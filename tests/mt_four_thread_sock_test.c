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

#define FOUR_MAX_SLOTS 4u
#define FOUR_FRAMES_PER_SENDER 100u
#define FOUR_MAX_HISTORY 220u

typedef struct {
    bool open;
    uint32_t stream_id;
    size_t slot_idx;
    size_t high_watermark;
    char expected[12288];
    size_t expected_len;
    char actual[12288];
    size_t actual_len;
} four_stream_rec_t;

typedef struct {
    int tx_fd;
    int rx_fd;
    pthread_mutex_t tx_mtx;
    pthread_mutex_t state_mtx;

    zb_stream_table_t tbl;
    uint8_t slot_rings[FOUR_MAX_SLOTS][48];
    four_stream_rec_t hist[FOUR_MAX_HISTORY];
    int active_hist_idx[FOUR_MAX_SLOTS];
    size_t hist_used;
    uint32_t next_sid;

    uint8_t rx_backing[ZB_RX_BUF_CAP];
    zb_rx_stream_t rx;

    uint64_t rng_a;
    uint64_t rng_b;
    uint64_t rx_rng;
    uint64_t mgr_rng;

    uint32_t active_senders;
    bool senders_all_done;
    bool rx_eof_reached;

    uint32_t frames_sent;
    uint32_t frames_demuxed;
    uint32_t frames_dropped_closed;
    uint32_t close_events;
    uint32_t reuse_events;
    uint32_t bp_events;
} four_ctx_t;

typedef struct {
    four_ctx_t *ctx;
    uint32_t sender_id;
    uint64_t *rng;
} four_sender_arg_t;

static uint64_t next_rng(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static four_stream_rec_t *find_rec_by_sid_locked(four_ctx_t *ctx, uint32_t sid) {
    for (size_t i = 0u; i < ctx->hist_used; i++) {
        if (ctx->hist[i].stream_id == sid) {
            return &ctx->hist[i];
        }
    }
    return NULL;
}

static size_t drain_rec_locked(four_ctx_t *ctx, four_stream_rec_t *rec) {
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

static bool rotate_one_slot_locked(four_ctx_t *ctx, uint64_t *rng) {
    if (ctx->hist_used >= FOUR_MAX_HISTORY) {
        return false;
    }

    size_t victim_slot = (size_t)(next_rng(rng) % FOUR_MAX_SLOTS);
    int old_hidx = ctx->active_hist_idx[victim_slot];
    if (old_hidx < 0 || !ctx->hist[old_hidx].open) {
        return false;
    }

    four_stream_rec_t *old_rec = &ctx->hist[old_hidx];
    drain_rec_locked(ctx, old_rec);

    uint32_t closed_sid = old_rec->stream_id;
    assert(zb_stream_close(&ctx->tbl, closed_sid) == ZB_STREAM_OK);
    assert(zb_stream_get(&ctx->tbl, closed_sid) == NULL);
    old_rec->open = false;
    ctx->close_events++;

    int new_hidx = (int)ctx->hist_used++;
    four_stream_rec_t *new_rec = &ctx->hist[new_hidx];
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
    return true;
}

static void *sender_worker(void *arg) {
    four_sender_arg_t *sarg = (four_sender_arg_t *)arg;
    four_ctx_t *ctx = sarg->ctx;
    uint64_t *rng = sarg->rng;

    for (uint32_t i = 0u; i < FOUR_FRAMES_PER_SENDER; i++) {
        uint32_t target_sid = 0u;
        char msg[32];
        size_t plen = 0u;

        pthread_mutex_lock(&ctx->state_mtx);
        size_t start_slot = (size_t)(next_rng(rng) % FOUR_MAX_SLOTS);
        for (size_t offset = 0u; offset < FOUR_MAX_SLOTS; offset++) {
            size_t s = (start_slot + offset) % FOUR_MAX_SLOTS;
            int hidx = ctx->active_hist_idx[s];
            if (hidx >= 0 && ctx->hist[hidx].open &&
                zb_stream_get(&ctx->tbl, ctx->hist[hidx].stream_id) != NULL) {
                target_sid = ctx->hist[hidx].stream_id;

                size_t want_len = (size_t)((next_rng(rng) % 10u) + 5u);
                int n = snprintf(msg, sizeof(msg), "f4-t%u-s%u-n%02u-",
                                 sarg->sender_id, target_sid, i);
                assert(n > 0);
                size_t cur = (size_t)n;
                while (cur < want_len && cur + 1u < sizeof(msg)) {
                    msg[cur] = (char)('A' + (char)((i + cur + sarg->sender_id) % 26u));
                    cur++;
                }
                msg[cur] = '\0';
                plen = cur;
                break;
            }
        }
        pthread_mutex_unlock(&ctx->state_mtx);

        if (target_sid == 0u) {
            continue;
        }

        uint8_t wire[128];
        size_t flen = zb_frame_encode(wire, sizeof(wire),
                                      (uint16_t)(1300u + (sarg->sender_id * 50u) + (i % 20u)),
                                      target_sid,
                                      (const uint8_t *)msg,
                                      (uint32_t)plen);
        assert(flen > 0u);

        pthread_mutex_lock(&ctx->tx_mtx);
        size_t off = 0u;
        while (off < flen) {
            size_t chunk = (size_t)((next_rng(rng) % 9u) + 1u);
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
        pthread_mutex_unlock(&ctx->tx_mtx);

        pthread_mutex_lock(&ctx->state_mtx);
        ctx->frames_sent++;
        pthread_mutex_unlock(&ctx->state_mtx);
    }

    pthread_mutex_lock(&ctx->tx_mtx);
    assert(ctx->active_senders > 0u);
    ctx->active_senders--;
    bool last_sender = (ctx->active_senders == 0u);
    if (last_sender && ctx->tx_fd >= 0) {
        close(ctx->tx_fd);
        ctx->tx_fd = -1;
    }
    pthread_mutex_unlock(&ctx->tx_mtx);

    if (last_sender) {
        pthread_mutex_lock(&ctx->state_mtx);
        ctx->senders_all_done = true;
        pthread_mutex_unlock(&ctx->state_mtx);
    }

    return NULL;
}

static void *receiver_thread(void *arg) {
    four_ctx_t *ctx = (four_ctx_t *)arg;
    uint8_t sock_buf[64];

    for (;;) {
        size_t want = (size_t)((next_rng(&ctx->rx_rng) % 14u) + 1u);
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

        pthread_mutex_lock(&ctx->state_mtx);
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

            four_stream_rec_t *target = find_rec_by_sid_locked(ctx, hdr.stream_id);
            if (target == NULL || !target->open ||
                zb_stream_get(&ctx->tbl, hdr.stream_id) == NULL) {
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

            for (size_t s = 0u; s < FOUR_MAX_SLOTS; s++) {
                int idx = ctx->active_hist_idx[s];
                if (idx >= 0 && ctx->hist[idx].open &&
                    (next_rng(&ctx->rx_rng) % 4u) == 0u) {
                    drain_rec_locked(ctx, &ctx->hist[idx]);
                }
            }
        }
        pthread_mutex_unlock(&ctx->state_mtx);
    }

    pthread_mutex_lock(&ctx->state_mtx);
    ctx->rx_eof_reached = true;
    for (size_t s = 0u; s < FOUR_MAX_SLOTS; s++) {
        int idx = ctx->active_hist_idx[s];
        if (idx >= 0 && ctx->hist[idx].open) {
            drain_rec_locked(ctx, &ctx->hist[idx]);
            assert(zb_stream_close(&ctx->tbl, ctx->hist[idx].stream_id) == ZB_STREAM_OK);
            ctx->hist[idx].open = false;
        }
    }
    pthread_mutex_unlock(&ctx->state_mtx);

    return NULL;
}

static void *lifecycle_mgr_thread(void *arg) {
    four_ctx_t *ctx = (four_ctx_t *)arg;
    uint32_t ticks = 0u;

    for (;;) {
        pthread_mutex_lock(&ctx->state_mtx);
        bool done = ctx->senders_all_done && ctx->rx_eof_reached;
        if (!done) {
            ticks++;
            if ((ticks % 3u) == 0u) {
                rotate_one_slot_locked(ctx, &ctx->mgr_rng);
            } else {
                for (size_t s = 0u; s < FOUR_MAX_SLOTS; s++) {
                    int idx = ctx->active_hist_idx[s];
                    if (idx >= 0 && ctx->hist[idx].open &&
                        (next_rng(&ctx->mgr_rng) % 3u) == 0u) {
                        drain_rec_locked(ctx, &ctx->hist[idx]);
                    }
                }
            }
        }
        pthread_mutex_unlock(&ctx->state_mtx);

        if (done) {
            break;
        }
        sched_yield();
    }

    return NULL;
}

int main(void) {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    four_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tx_fd = sv[0];
    ctx.rx_fd = sv[1];
    ctx.rng_a = 0x9E3779B97F4A7C15ULL;
    ctx.rng_b = 0xBF58476D1CE4E5B9ULL;
    ctx.rx_rng = 0x94D049BB133111EBULL;
    ctx.mgr_rng = 0xD6E8FEB86659FD93ULL;
    ctx.next_sid = 1u;
    ctx.active_senders = 2u;

    assert(pthread_mutex_init(&ctx.tx_mtx, NULL) == 0);
    assert(pthread_mutex_init(&ctx.state_mtx, NULL) == 0);
    zb_stream_table_init(&ctx.tbl);
    assert(zb_rx_init(&ctx.rx, ctx.rx_backing, sizeof(ctx.rx_backing)) == ZB_OK);

    for (size_t i = 0u; i < FOUR_MAX_SLOTS; i++) {
        ctx.active_hist_idx[i] = -1;
    }

    for (size_t s = 0u; s < FOUR_MAX_SLOTS; s++) {
        int hidx = (int)ctx.hist_used++;
        four_stream_rec_t *rec = &ctx.hist[hidx];
        rec->open = true;
        rec->stream_id = ctx.next_sid++;
        rec->slot_idx = s;
        rec->high_watermark = (s == 0u) ? 20u : 32u;

        assert(zb_stream_open(&ctx.tbl, rec->stream_id,
                              ctx.slot_rings[s], sizeof(ctx.slot_rings[s]),
                              rec->high_watermark) == ZB_STREAM_OK);
        ctx.active_hist_idx[s] = hidx;
    }

    four_sender_arg_t arg_a = { &ctx, 1u, &ctx.rng_a };
    four_sender_arg_t arg_b = { &ctx, 2u, &ctx.rng_b };

    pthread_t th_a, th_b, th_rx, th_mgr;
    assert(pthread_create(&th_a, NULL, sender_worker, &arg_a) == 0);
    assert(pthread_create(&th_b, NULL, sender_worker, &arg_b) == 0);
    assert(pthread_create(&th_rx, NULL, receiver_thread, &ctx) == 0);
    assert(pthread_create(&th_mgr, NULL, lifecycle_mgr_thread, &ctx) == 0);

    assert(pthread_join(th_a, NULL) == 0);
    assert(pthread_join(th_b, NULL) == 0);
    assert(pthread_join(th_rx, NULL) == 0);
    assert(pthread_join(th_mgr, NULL) == 0);

    close(ctx.rx_fd);
    assert(ctx.senders_all_done);
    assert(ctx.rx_eof_reached);
    assert(ctx.tbl.count == 0u);
    assert(ctx.frames_sent > 0u);
    assert(ctx.frames_demuxed == ctx.frames_sent);
    assert(ctx.close_events > 0u);
    assert(ctx.reuse_events == ctx.close_events);

    for (size_t i = 0u; i < ctx.hist_used; i++) {
        four_stream_rec_t *rec = &ctx.hist[i];
        rec->expected[rec->expected_len] = '\0';
        rec->actual[rec->actual_len] = '\0';
        assert(rec->actual_len == rec->expected_len);
        assert(strcmp(rec->actual, rec->expected) == 0);
    }

    pthread_mutex_destroy(&ctx.tx_mtx);
    pthread_mutex_destroy(&ctx.state_mtx);

    printf("mt_four_thread_sock_test passed! sent=%u demuxed=%u dropped_closed=%u closes=%u reuses=%u bp=%u sids=%zu\n",
           ctx.frames_sent, ctx.frames_demuxed, ctx.frames_dropped_closed,
           ctx.close_events, ctx.reuse_events, ctx.bp_events, ctx.hist_used);
    return 0;
}
