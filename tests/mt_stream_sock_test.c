#define _POSIX_C_SOURCE 200809L
#include "zero_bus.h"
#include "zb_stream.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MT_MAX_SLOTS 4u
#define MT_TOTAL_FRAMES 120u
#define MT_MAX_HISTORY 160u

typedef struct {
    bool open;
    uint32_t stream_id;
    size_t slot_idx;
    size_t high_watermark;
    char expected[4096];
    size_t expected_len;
    char actual[4096];
    size_t actual_len;
} mt_stream_rec_t;

typedef struct {
    int tx_fd;
    int rx_fd;
    pthread_mutex_t mtx;

    zb_stream_table_t tbl;
    uint8_t slot_rings[MT_MAX_SLOTS][48];
    mt_stream_rec_t hist[MT_MAX_HISTORY];
    int active_hist_idx[MT_MAX_SLOTS];
    size_t hist_used;
    uint32_t next_sid;

    uint8_t rx_backing[ZB_RX_BUF_CAP];
    zb_rx_stream_t rx;

    uint64_t send_rng;
    uint64_t rx_rng;

    bool sender_done;
    uint32_t frames_sent;
    uint32_t frames_demuxed;
    uint32_t frames_dropped_closed;
    uint32_t close_events;
    uint32_t reuse_events;
    uint32_t bp_events;
} mt_ctx_t;

static uint64_t next_rng(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static mt_stream_rec_t *find_rec_by_sid_locked(mt_ctx_t *ctx, uint32_t sid) {
    for (size_t i = 0u; i < ctx->hist_used; i++) {
        if (ctx->hist[i].stream_id == sid) {
            return &ctx->hist[i];
        }
    }
    return NULL;
}

static size_t drain_rec_locked(mt_ctx_t *ctx, mt_stream_rec_t *rec) {
    uint8_t tmp[32];
    size_t total = 0u;
    for (;;) {
        size_t got = zb_stream_pop(&ctx->tbl, rec->stream_id, tmp, sizeof(tmp));
        if (got == 0u) {
            break;
        }
        assert(rec->actual_len + got < sizeof(rec->actual));
        memcpy(rec->actual + rec->actual_len, tmp, got);
        rec->actual_len += got;
        total += got;
    }
    return total;
}

static void maybe_rotate_slot_locked(mt_ctx_t *ctx, uint32_t frame_seq) {
    if (frame_seq == 0u || (frame_seq % 15u) != 0u) {
        return;
    }
    if (ctx->hist_used >= MT_MAX_HISTORY) {
        return;
    }

    size_t victim_slot = (size_t)(next_rng(&ctx->rx_rng) % MT_MAX_SLOTS);
    int old_hidx = ctx->active_hist_idx[victim_slot];
    assert(old_hidx >= 0);
    mt_stream_rec_t *old_rec = &ctx->hist[old_hidx];
    assert(old_rec->open);

    drain_rec_locked(ctx, old_rec);

    uint32_t closed_sid = old_rec->stream_id;
    assert(zb_stream_close(&ctx->tbl, closed_sid) == ZB_STREAM_OK);
    assert(zb_stream_get(&ctx->tbl, closed_sid) == NULL);
    old_rec->open = false;
    ctx->close_events++;

    int new_hidx = (int)ctx->hist_used++;
    mt_stream_rec_t *new_rec = &ctx->hist[new_hidx];
    new_rec->open = true;
    new_rec->stream_id = ctx->next_sid++;
    new_rec->slot_idx = victim_slot;
    new_rec->high_watermark = (victim_slot == 0u) ? 20u : 32u;

    assert(zb_stream_open(&ctx->tbl, new_rec->stream_id,
                          ctx->slot_rings[victim_slot],
                          sizeof(ctx->slot_rings[victim_slot]),
                          new_rec->high_watermark) == ZB_STREAM_OK);
    ctx->active_hist_idx[victim_slot] = new_hidx;
    ctx->reuse_events++;
}

static void *sender_thread(void *arg) {
    mt_ctx_t *ctx = (mt_ctx_t *)arg;

    for (uint32_t i = 0u; i < MT_TOTAL_FRAMES; i++) {
        uint32_t target_sid = 0u;
        char msg[28];
        size_t plen = 0u;

        pthread_mutex_lock(&ctx->mtx);
        size_t start_slot = (size_t)(next_rng(&ctx->send_rng) % MT_MAX_SLOTS);
        for (size_t offset = 0u; offset < MT_MAX_SLOTS; offset++) {
            size_t s = (start_slot + offset) % MT_MAX_SLOTS;
            int hidx = ctx->active_hist_idx[s];
            if (hidx >= 0 && ctx->hist[hidx].open &&
                zb_stream_get(&ctx->tbl, ctx->hist[hidx].stream_id) != NULL) {
                target_sid = ctx->hist[hidx].stream_id;

                size_t want_len = (size_t)((next_rng(&ctx->send_rng) % 12u) + 4u);
                int n = snprintf(msg, sizeof(msg), "mt%u-f%03u-", target_sid, i);
                assert(n > 0);
                size_t cur = (size_t)n;
                while (cur < want_len && cur + 1u < sizeof(msg)) {
                    msg[cur] = (char)('a' + (char)((i + cur) % 26u));
                    cur++;
                }
                msg[cur] = '\0';
                plen = cur;
                break;
            }
        }
        pthread_mutex_unlock(&ctx->mtx);

        if (target_sid == 0u) {
            continue;
        }

        uint8_t wire[128];
        size_t flen = zb_frame_encode(wire, sizeof(wire),
                                      (uint16_t)(900u + (i % 30u)),
                                      target_sid,
                                      (const uint8_t *)msg,
                                      (uint32_t)plen);
        assert(flen > 0u);

        size_t off = 0u;
        while (off < flen) {
            size_t chunk = (size_t)((next_rng(&ctx->send_rng) % 10u) + 1u);
            if (chunk > flen - off) {
                chunk = flen - off;
            }
            ssize_t w = send(ctx->tx_fd, wire + off, chunk, 0);
            if (w < 0 && errno == EINTR) {
                continue;
            }
            assert(w > 0);
            off += (size_t)w;
        }

        pthread_mutex_lock(&ctx->mtx);
        ctx->frames_sent++;
        pthread_mutex_unlock(&ctx->mtx);
    }

    pthread_mutex_lock(&ctx->mtx);
    ctx->sender_done = true;
    pthread_mutex_unlock(&ctx->mtx);

    close(ctx->tx_fd);
    ctx->tx_fd = -1;
    return NULL;
}

static void *receiver_mgr_thread(void *arg) {
    mt_ctx_t *ctx = (mt_ctx_t *)arg;
    uint8_t sock_buf[64];

    for (;;) {
        size_t want = (size_t)((next_rng(&ctx->rx_rng) % 15u) + 1u);
        if (want > sizeof(sock_buf)) {
            want = sizeof(sock_buf);
        }
        ssize_t r = recv(ctx->rx_fd, sock_buf, want, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            assert(false);
        }
        if (r == 0) {
            break;
        }

        pthread_mutex_lock(&ctx->mtx);
        assert(zb_rx_push(&ctx->rx, sock_buf, (size_t)r) == ZB_OK);

        zb_hdr_t hdr;
        uint8_t payload[64];
        for (;;) {
            zb_status_t st = zb_rx_pop_frame(&ctx->rx, &hdr, payload, sizeof(payload));
            if (st == ZB_ERR_NEED_MORE) {
                break;
            }
            assert(st == ZB_OK);
            ctx->frames_demuxed++;

            mt_stream_rec_t *target = find_rec_by_sid_locked(ctx, hdr.stream_id);
            if (target == NULL || !target->open ||
                zb_stream_get(&ctx->tbl, hdr.stream_id) == NULL) {
                /* Stream closed while this frame was in flight on the socket */
                ctx->frames_dropped_closed++;
                continue;
            }

            zb_stream_status_t pst = zb_stream_push(&ctx->tbl, hdr.stream_id,
                                                    payload, hdr.payload_len);
            while (pst == ZB_STREAM_ERR_BACKPRESSURE) {
                ctx->bp_events++;
                size_t drained = drain_rec_locked(ctx, target);
                assert(drained > 0u);
                pst = zb_stream_push(&ctx->tbl, hdr.stream_id,
                                     payload, hdr.payload_len);
            }
            assert(pst == ZB_STREAM_OK);

            assert(target->expected_len + hdr.payload_len < sizeof(target->expected));
            memcpy(target->expected + target->expected_len, payload, hdr.payload_len);
            target->expected_len += hdr.payload_len;

            for (size_t s = 0u; s < MT_MAX_SLOTS; s++) {
                int idx = ctx->active_hist_idx[s];
                if (idx >= 0 && ctx->hist[idx].open &&
                    (next_rng(&ctx->rx_rng) % 4u) == 0u) {
                    drain_rec_locked(ctx, &ctx->hist[idx]);
                }
            }

            maybe_rotate_slot_locked(ctx, ctx->frames_demuxed);
        }
        pthread_mutex_unlock(&ctx->mtx);
    }

    pthread_mutex_lock(&ctx->mtx);
    for (size_t s = 0u; s < MT_MAX_SLOTS; s++) {
        int idx = ctx->active_hist_idx[s];
        if (idx >= 0 && ctx->hist[idx].open) {
            drain_rec_locked(ctx, &ctx->hist[idx]);
            assert(zb_stream_close(&ctx->tbl, ctx->hist[idx].stream_id) == ZB_STREAM_OK);
            ctx->hist[idx].open = false;
        }
    }
    pthread_mutex_unlock(&ctx->mtx);

    return NULL;
}

int main(void) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    mt_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tx_fd = sv[0];
    ctx.rx_fd = sv[1];
    ctx.send_rng = 0x243F6A8885A308D3ULL;
    ctx.rx_rng = 0x13198A2E03707344ULL;
    ctx.next_sid = 1u;

    assert(pthread_mutex_init(&ctx.mtx, NULL) == 0);
    zb_stream_table_init(&ctx.tbl);
    assert(zb_rx_init(&ctx.rx, ctx.rx_backing, sizeof(ctx.rx_backing)) == ZB_OK);

    for (size_t i = 0u; i < MT_MAX_SLOTS; i++) {
        ctx.active_hist_idx[i] = -1;
    }

    for (size_t s = 0u; s < MT_MAX_SLOTS; s++) {
        int hidx = (int)ctx.hist_used++;
        mt_stream_rec_t *rec = &ctx.hist[hidx];
        rec->open = true;
        rec->stream_id = ctx.next_sid++;
        rec->slot_idx = s;
        rec->high_watermark = (s == 0u) ? 20u : 32u;

        assert(zb_stream_open(&ctx.tbl, rec->stream_id,
                              ctx.slot_rings[s], sizeof(ctx.slot_rings[s]),
                              rec->high_watermark) == ZB_STREAM_OK);
        ctx.active_hist_idx[s] = hidx;
    }

    pthread_t th_tx, th_rx;
    assert(pthread_create(&th_tx, NULL, sender_thread, &ctx) == 0);
    assert(pthread_create(&th_rx, NULL, receiver_mgr_thread, &ctx) == 0);

    assert(pthread_join(th_tx, NULL) == 0);
    assert(pthread_join(th_rx, NULL) == 0);

    close(ctx.rx_fd);
    assert(ctx.sender_done);
    assert(ctx.tbl.count == 0u);
    assert(ctx.frames_sent > 0u);
    assert(ctx.frames_demuxed == ctx.frames_sent);
    assert(ctx.close_events > 0u);
    assert(ctx.reuse_events == ctx.close_events);

    for (size_t i = 0u; i < ctx.hist_used; i++) {
        mt_stream_rec_t *rec = &ctx.hist[i];
        rec->expected[rec->expected_len] = '\0';
        rec->actual[rec->actual_len] = '\0';
        assert(rec->actual_len == rec->expected_len);
        assert(strcmp(rec->actual, rec->expected) == 0);
    }

    pthread_mutex_destroy(&ctx.mtx);

    printf("mt_stream_sock_test passed! sent=%u demuxed=%u dropped_closed=%u closes=%u reuses=%u bp=%u sids=%zu\n",
           ctx.frames_sent, ctx.frames_demuxed, ctx.frames_dropped_closed,
           ctx.close_events, ctx.reuse_events, ctx.bp_events, ctx.hist_used);
    return 0;
}
