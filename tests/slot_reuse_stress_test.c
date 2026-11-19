#include "zero_bus.h"
#include "zb_stream.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

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
} stream_record_t;

static uint64_t next_rng(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static size_t drain_stream_into_record(zb_stream_table_t *tbl, stream_record_t *rec) {
    uint8_t tmp[32];
    size_t total_got = 0u;
    for (;;) {
        size_t got = zb_stream_pop(tbl, rec->stream_id, tmp, sizeof(tmp));
        if (got == 0u) {
            break;
        }
        assert(rec->actual_len + got < sizeof(rec->actual));
        memcpy(rec->actual + rec->actual_len, tmp, got);
        rec->actual_len += got;
        total_got += got;
    }
    return total_got;
}

int main(void) {
    uint64_t rng = 0x9E3779B97F4A7C15ULL;

    zb_stream_table_t tbl;
    zb_stream_table_init(&tbl);

    uint8_t slot_rings[MAX_ACTIVE_SLOTS][40];
    stream_record_t hist[MAX_STREAM_HISTORY];
    memset(hist, 0, sizeof(hist));

    int active_hist_idx[MAX_ACTIVE_SLOTS];
    for (size_t i = 0u; i < MAX_ACTIVE_SLOTS; i++) {
        active_hist_idx[i] = -1;
    }

    uint32_t next_sid = 1u;
    size_t hist_used = 0u;

    /* Open initial MAX_ACTIVE_SLOTS streams */
    for (size_t s = 0u; s < MAX_ACTIVE_SLOTS; s++) {
        assert(hist_used < MAX_STREAM_HISTORY);
        int hidx = (int)hist_used++;
        stream_record_t *rec = &hist[hidx];
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
    uint32_t total_frames_demuxed = 0u;
    uint32_t bp_events = 0u;

    for (uint32_t step = 0u; step < TOTAL_CYCLES; step++) {
        /* Periodically close one active slot and open a brand-new stream_id in it */
        if (step > 0u && (step % 12u) == 0u) {
            size_t victim_slot = (size_t)(next_rng(&rng) % MAX_ACTIVE_SLOTS);
            int old_hidx = active_hist_idx[victim_slot];
            assert(old_hidx >= 0);
            stream_record_t *old_rec = &hist[old_hidx];
            assert(old_rec->open);

            /* Drain all pending bytes before closing so actual matches expected */
            drain_stream_into_record(&tbl, old_rec);

            uint32_t closed_sid = old_rec->stream_id;
            assert(zb_stream_close(&tbl, closed_sid) == ZB_STREAM_OK);
            assert(zb_stream_get(&tbl, closed_sid) == NULL);
            old_rec->open = false;
            close_events++;

            /* Immediately open a new stream_id in the freed slot */
            assert(hist_used < MAX_STREAM_HISTORY);
            int new_hidx = (int)hist_used++;
            stream_record_t *new_rec = &hist[new_hidx];
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

        /* Pick a currently open stream */
        size_t pick_slot = (size_t)(next_rng(&rng) % MAX_ACTIVE_SLOTS);
        int hidx = active_hist_idx[pick_slot];
        assert(hidx >= 0);
        stream_record_t *rec = &hist[hidx];
        assert(rec->open);

        size_t plen = (size_t)((next_rng(&rng) % 12u) + 4u); /* 4..15 bytes */
        char msg[24];
        int n = snprintf(msg, sizeof(msg), "st%u-c%02u-", rec->stream_id, step);
        assert(n > 0);
        size_t cur = (size_t)n;
        while (cur < plen && cur + 1u < sizeof(msg)) {
            msg[cur] = (char)('a' + (char)((step + cur) % 26u));
            cur++;
        }
        msg[cur] = '\0';
        plen = cur;

        uint8_t wire[128];
        size_t flen = zb_frame_encode(wire, sizeof(wire),
                                      (uint16_t)(500u + (step % 20u)),
                                      rec->stream_id,
                                      (const uint8_t *)msg,
                                      (uint32_t)plen);
        assert(flen > 0u);

        assert(rec->expected_len + plen < sizeof(rec->expected));
        memcpy(rec->expected + rec->expected_len, msg, plen);
        rec->expected_len += plen;
        rec->frames_pushed++;

        /* Feed wire bytes in variable chunks (1..9 bytes) */
        size_t off = 0u;
        while (off < flen) {
            size_t chunk = (size_t)((next_rng(&rng) % 9u) + 1u);
            if (chunk > flen - off) {
                chunk = flen - off;
            }
            assert(zb_rx_push(&rx, wire + off, chunk) == ZB_OK);
            off += chunk;

            zb_hdr_t hdr;
            uint8_t payload[64];
            for (;;) {
                zb_status_t st = zb_rx_pop_frame(&rx, &hdr, payload, sizeof(payload));
                if (st == ZB_ERR_NEED_MORE) {
                    break;
                }
                assert(st == ZB_OK);
                assert(hdr.stream_id == rec->stream_id);
                total_frames_demuxed++;

                zb_stream_status_t pst = zb_stream_push(&tbl, hdr.stream_id,
                                                        payload, hdr.payload_len);
                while (pst == ZB_STREAM_ERR_BACKPRESSURE) {
                    bp_events++;
                    size_t drained = drain_stream_into_record(&tbl, rec);
                    assert(drained > 0u);
                    pst = zb_stream_push(&tbl, hdr.stream_id,
                                         payload, hdr.payload_len);
                }
                assert(pst == ZB_STREAM_OK);
            }
        }

        /* Periodically drain other open streams to keep small rings flowing */
        for (size_t s = 0u; s < MAX_ACTIVE_SLOTS; s++) {
            if (s == pick_slot) {
                continue;
            }
            int other_idx = active_hist_idx[s];
            if (other_idx >= 0 && hist[other_idx].open && (next_rng(&rng) % 4u) == 0u) {
                drain_stream_into_record(&tbl, &hist[other_idx]);
            }
        }
    }

    /* Drain and close all remaining open streams */
    for (size_t s = 0u; s < MAX_ACTIVE_SLOTS; s++) {
        int hidx = active_hist_idx[s];
        if (hidx >= 0 && hist[hidx].open) {
            drain_stream_into_record(&tbl, &hist[hidx]);
            assert(zb_stream_close(&tbl, hist[hidx].stream_id) == ZB_STREAM_OK);
            hist[hidx].open = false;
        }
    }

    assert(tbl.count == 0u);
    assert(close_events > 0u);
    assert(reuse_events == close_events);
    assert(total_frames_demuxed == TOTAL_CYCLES);

    for (size_t i = 0u; i < hist_used; i++) {
        stream_record_t *rec = &hist[i];
        rec->expected[rec->expected_len] = '\0';
        rec->actual[rec->actual_len] = '\0';
        assert(rec->actual_len == rec->expected_len);
        assert(strcmp(rec->actual, rec->expected) == 0);
    }

    printf("slot_reuse_stress_test passed! cycles=%u closes=%u reuses=%u bp=%u total_sids=%zu\n",
           total_frames_demuxed, close_events, reuse_events, bp_events, hist_used);
    return 0;
}
