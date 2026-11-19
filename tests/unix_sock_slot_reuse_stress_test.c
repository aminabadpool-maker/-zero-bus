#define _POSIX_C_SOURCE 200809L
#include "zero_bus.h"
#include "zb_stream.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_ACTIVE_SLOTS 4u
#define TOTAL_CYCLES 96u
#define MAX_STREAM_HISTORY 128u

typedef struct {
    bool open;
    uint32_t stream_id;
    size_t slot_idx;
    size_t high_watermark;
    char expected[2048];
    size_t expected_len;
    uint32_t frames_pushed;
    char actual[2048];
    size_t actual_len;
} unix_stream_rec_t;

static uint64_t next_rng(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static size_t drain_stream(zb_stream_table_t *tbl, unix_stream_rec_t *rec) {
    uint8_t tmp[32];
    size_t got_total = 0u;
    for (;;) {
        size_t got = zb_stream_pop(tbl, rec->stream_id, tmp, sizeof(tmp));
        if (got == 0u) {
            break;
        }
        assert(rec->actual_len + got < sizeof(rec->actual));
        memcpy(rec->actual + rec->actual_len, tmp, got);
        rec->actual_len += got;
        got_total += got;
    }
    return got_total;
}

static void write_all_sock(int fd, const uint8_t *buf, size_t len, uint64_t *rng) {
    size_t off = 0u;
    while (off < len) {
        size_t chunk = (size_t)((next_rng(rng) % 11u) + 1u);
        if (chunk > len - off) {
            chunk = len - off;
        }
        ssize_t w = send(fd, buf + off, chunk, 0);
        if (w < 0 && errno == EINTR) {
            continue;
        }
        assert(w > 0);
        off += (size_t)w;
    }
}

static void consume_socket_frames(int rx_fd, zb_rx_stream_t *rx,
                                  zb_stream_table_t *tbl,
                                  unix_stream_rec_t *hist, size_t hist_used,
                                  const int *active_hist_idx,
                                  uint32_t *total_demuxed, uint32_t *bp_events,
                                  uint64_t *rng) {
    uint8_t sock_buf[64];
    for (;;) {
        size_t want = (size_t)((next_rng(rng) % 13u) + 1u);
        if (want > sizeof(sock_buf)) {
            want = sizeof(sock_buf);
        }
        ssize_t r = recv(rx_fd, sock_buf, want, MSG_DONTWAIT);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            assert(false);
        }
        if (r == 0) {
            break;
        }

        assert(zb_rx_push(rx, sock_buf, (size_t)r) == ZB_OK);

        zb_hdr_t hdr;
        uint8_t payload[64];
        for (;;) {
            zb_status_t st = zb_rx_pop_frame(rx, &hdr, payload, sizeof(payload));
            if (st == ZB_ERR_NEED_MORE) {
                break;
            }
            assert(st == ZB_OK);
            (*total_demuxed)++;

            unix_stream_rec_t *target = NULL;
            for (size_t i = 0u; i < hist_used; i++) {
                if (hist[i].stream_id == hdr.stream_id && hist[i].open) {
                    target = &hist[i];
                    break;
                }
            }
            assert(target != NULL);

            zb_stream_status_t pst = zb_stream_push(tbl, hdr.stream_id,
                                                    payload, hdr.payload_len);
            while (pst == ZB_STREAM_ERR_BACKPRESSURE) {
                (*bp_events)++;
                size_t drained = drain_stream(tbl, target);
                assert(drained > 0u);
                pst = zb_stream_push(tbl, hdr.stream_id,
                                     payload, hdr.payload_len);
            }
            assert(pst == ZB_STREAM_OK);

            for (size_t s = 0u; s < MAX_ACTIVE_SLOTS; s++) {
                int idx = active_hist_idx[s];
                if (idx >= 0 && hist[idx].open && (next_rng(rng) % 3u) == 0u) {
                    drain_stream(tbl, &hist[idx]);
                }
            }
        }
    }
}

int main(void) {
    uint64_t rng = 0xD1B54A32D192ED03ULL;

    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int tx_fd = sv[0];
    int rx_fd = sv[1];

    zb_stream_table_t tbl;
    zb_stream_table_init(&tbl);

    uint8_t slot_rings[MAX_ACTIVE_SLOTS][40];
    unix_stream_rec_t hist[MAX_STREAM_HISTORY];
    memset(hist, 0, sizeof(hist));

    int active_hist_idx[MAX_ACTIVE_SLOTS];
    for (size_t i = 0u; i < MAX_ACTIVE_SLOTS; i++) {
        active_hist_idx[i] = -1;
    }

    uint32_t next_sid = 1u;
    size_t hist_used = 0u;

    for (size_t s = 0u; s < MAX_ACTIVE_SLOTS; s++) {
        assert(hist_used < MAX_STREAM_HISTORY);
        int hidx = (int)hist_used++;
        unix_stream_rec_t *rec = &hist[hidx];
        rec->open = true;
        rec->stream_id = next_sid++;
        rec->slot_idx = s;
        rec->high_watermark = (s == 0u) ? 18u : 28u;

        assert(zb_stream_open(&tbl, rec->stream_id,
                              slot_rings[s], sizeof(slot_rings[s]),
                              rec->high_watermark) == ZB_STREAM_OK);
        active_hist_idx[s] = hidx;
    }

    uint8_t rx_backing[ZB_RX_BUF_CAP];
    zb_rx_stream_t rx;
    assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);

    uint32_t close_events = 0u;
    uint32_t reuse_events = 0u;
    uint32_t total_demuxed = 0u;
    uint32_t bp_events = 0u;

    for (uint32_t step = 0u; step < TOTAL_CYCLES; step++) {
        /* Periodically close an active stream mid-transfer and reuse its slot */
        if (step > 0u && (step % 12u) == 0u) {
            /* Flush any in-flight bytes on the socket so prior frames are delivered */
            consume_socket_frames(rx_fd, &rx, &tbl, hist, hist_used,
                                  active_hist_idx, &total_demuxed, &bp_events, &rng);

            size_t victim_slot = (size_t)(next_rng(&rng) % MAX_ACTIVE_SLOTS);
            int old_hidx = active_hist_idx[victim_slot];
            assert(old_hidx >= 0);
            unix_stream_rec_t *old_rec = &hist[old_hidx];
            assert(old_rec->open);

            drain_stream(&tbl, old_rec);

            uint32_t closed_sid = old_rec->stream_id;
            assert(zb_stream_close(&tbl, closed_sid) == ZB_STREAM_OK);
            assert(zb_stream_get(&tbl, closed_sid) == NULL);
            old_rec->open = false;
            close_events++;

            assert(hist_used < MAX_STREAM_HISTORY);
            int new_hidx = (int)hist_used++;
            unix_stream_rec_t *new_rec = &hist[new_hidx];
            new_rec->open = true;
            new_rec->stream_id = next_sid++;
            new_rec->slot_idx = victim_slot;
            new_rec->high_watermark = (victim_slot == 0u) ? 18u : 28u;

            assert(zb_stream_open(&tbl, new_rec->stream_id,
                                  slot_rings[victim_slot], sizeof(slot_rings[victim_slot]),
                                  new_rec->high_watermark) == ZB_STREAM_OK);
            active_hist_idx[victim_slot] = new_hidx;
            reuse_events++;
        }

        size_t pick_slot = (size_t)(next_rng(&rng) % MAX_ACTIVE_SLOTS);
        int hidx = active_hist_idx[pick_slot];
        assert(hidx >= 0);
        unix_stream_rec_t *rec = &hist[hidx];
        assert(rec->open);

        size_t plen = (size_t)((next_rng(&rng) % 12u) + 4u);
        char msg[24];
        int n = snprintf(msg, sizeof(msg), "us%u-s%02u-", rec->stream_id, step);
        assert(n > 0);
        size_t cur = (size_t)n;
        while (cur < plen && cur + 1u < sizeof(msg)) {
            msg[cur] = (char)('A' + (char)((step + cur) % 26u));
            cur++;
        }
        msg[cur] = '\0';
        plen = cur;

        uint8_t wire[128];
        size_t flen = zb_frame_encode(wire, sizeof(wire),
                                      (uint16_t)(700u + (step % 25u)),
                                      rec->stream_id,
                                      (const uint8_t *)msg,
                                      (uint32_t)plen);
        assert(flen > 0u);

        assert(rec->expected_len + plen < sizeof(rec->expected));
        memcpy(rec->expected + rec->expected_len, msg, plen);
        rec->expected_len += plen;
        rec->frames_pushed++;

        write_all_sock(tx_fd, wire, flen, &rng);
        consume_socket_frames(rx_fd, &rx, &tbl, hist, hist_used,
                              active_hist_idx, &total_demuxed, &bp_events, &rng);
    }

    close(tx_fd);
    tx_fd = -1;

    /* Final drain of socket and all open streams */
    for (int guard = 0; guard < 128 && total_demuxed < TOTAL_CYCLES; guard++) {
        consume_socket_frames(rx_fd, &rx, &tbl, hist, hist_used,
                              active_hist_idx, &total_demuxed, &bp_events, &rng);
    }

    for (size_t s = 0u; s < MAX_ACTIVE_SLOTS; s++) {
        int hidx = active_hist_idx[s];
        if (hidx >= 0 && hist[hidx].open) {
            drain_stream(&tbl, &hist[hidx]);
            assert(zb_stream_close(&tbl, hist[hidx].stream_id) == ZB_STREAM_OK);
            hist[hidx].open = false;
        }
    }

    close(rx_fd);
    assert(tbl.count == 0u);
    assert(close_events > 0u);
    assert(reuse_events == close_events);
    assert(total_demuxed == TOTAL_CYCLES);

    for (size_t i = 0u; i < hist_used; i++) {
        unix_stream_rec_t *rec = &hist[i];
        rec->expected[rec->expected_len] = '\0';
        rec->actual[rec->actual_len] = '\0';
        assert(rec->actual_len == rec->expected_len);
        assert(strcmp(rec->actual, rec->expected) == 0);
    }

    printf("unix_sock_slot_reuse_stress_test passed! cycles=%u closes=%u reuses=%u bp=%u sids=%zu\n",
           total_demuxed, close_events, reuse_events, bp_events, hist_used);
    return 0;
}
