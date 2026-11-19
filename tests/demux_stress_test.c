#include "zero_bus.h"
#include "zb_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define STRESS_TOTAL_FRAMES 120u
#define NUM_STREAMS 4u

static uint64_t next_rng(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

int main(void) {
    uint64_t rng = 0xD1E5A7B92026ULL;

    zb_stream_table_t tbl;
    zb_stream_table_init(&tbl);

    uint8_t rings[NUM_STREAMS][40];
    const size_t watermarks[NUM_STREAMS] = { 20u, 32u, 32u, 36u };

    for (uint32_t s = 0u; s < NUM_STREAMS; s++) {
        assert(zb_stream_open(&tbl, s + 1u, rings[s], sizeof(rings[s]), watermarks[s]) == ZB_STREAM_OK);
    }

    char expected[NUM_STREAMS][4096];
    size_t expected_len[NUM_STREAMS] = { 0u, 0u, 0u, 0u };
    uint32_t expected_frames[NUM_STREAMS] = { 0u, 0u, 0u, 0u };

    uint8_t wire[16384];
    size_t wire_used = 0u;

    for (uint32_t f = 0u; f < STRESS_TOTAL_FRAMES; f++) {
        uint32_t sid = (uint32_t)((next_rng(&rng) % NUM_STREAMS) + 1u);
        uint16_t mtype = (uint16_t)(300u + (f % 32u));
        size_t plen = (size_t)((next_rng(&rng) % 14u) + 4u); /* 4..17 bytes */

        char msg[32];
        int n = snprintf(msg, sizeof(msg), "s%u-f%03u-", sid, f);
        assert(n > 0);
        size_t cur = (size_t)n;
        while (cur < plen && cur + 1u < sizeof(msg)) {
            msg[cur] = (char)('a' + (char)((f + cur) % 26u));
            cur++;
        }
        msg[cur] = '\0';
        plen = cur;

        size_t flen = zb_frame_encode(wire + wire_used, sizeof(wire) - wire_used,
                                      mtype, sid, (const uint8_t *)msg, (uint32_t)plen);
        assert(flen > 0u);
        wire_used += flen;

        assert(expected_len[sid - 1u] + plen < sizeof(expected[sid - 1u]));
        memcpy(expected[sid - 1u] + expected_len[sid - 1u], msg, plen);
        expected_len[sid - 1u] += plen;
        expected_frames[sid - 1u]++;
    }

    char actual[NUM_STREAMS][4096];
    size_t actual_len[NUM_STREAMS] = { 0u, 0u, 0u, 0u };

    uint8_t rx_backing[ZB_RX_BUF_CAP];
    zb_rx_stream_t rx;
    assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);

    zb_hdr_t hdr;
    uint8_t payload[128];
    uint8_t drain[32];

    uint32_t decoded_total = 0u;
    uint32_t bp_events = 0u;
    size_t off = 0u;

    while (off < wire_used) {
        size_t chunk = (size_t)((next_rng(&rng) % 19u) + 1u); /* variable 1..19 bytes */
        if (chunk > wire_used - off) {
            chunk = wire_used - off;
        }
        assert(zb_rx_push(&rx, wire + off, chunk) == ZB_OK);
        off += chunk;

        for (;;) {
            zb_status_t st = zb_rx_pop_frame(&rx, &hdr, payload, sizeof(payload));
            if (st == ZB_ERR_NEED_MORE) {
                break;
            }
            assert(st == ZB_OK);
            assert(hdr.stream_id >= 1u && hdr.stream_id <= NUM_STREAMS);
            decoded_total++;

            zb_stream_status_t pst = zb_stream_push(&tbl, hdr.stream_id,
                                                    payload, hdr.payload_len);
            while (pst == ZB_STREAM_ERR_BACKPRESSURE) {
                bp_events++;
                size_t got = zb_stream_pop(&tbl, hdr.stream_id, drain, sizeof(drain));
                assert(got > 0u);
                size_t idx = (size_t)(hdr.stream_id - 1u);
                assert(actual_len[idx] + got < sizeof(actual[idx]));
                memcpy(actual[idx] + actual_len[idx], drain, got);
                actual_len[idx] += got;

                pst = zb_stream_push(&tbl, hdr.stream_id, payload, hdr.payload_len);
            }
            assert(pst == ZB_STREAM_OK);
        }

        /* Periodically drain non-priority streams so small rings never stall */
        for (uint32_t s = 2u; s <= NUM_STREAMS; s++) {
            if ((next_rng(&rng) % 3u) == 0u) {
                size_t got = zb_stream_pop(&tbl, s, drain, sizeof(drain));
                if (got > 0u) {
                    size_t idx = (size_t)(s - 1u);
                    assert(actual_len[idx] + got < sizeof(actual[idx]));
                    memcpy(actual[idx] + actual_len[idx], drain, got);
                    actual_len[idx] += got;
                }
            }
        }
    }

    assert(decoded_total == STRESS_TOTAL_FRAMES);
    assert(bp_events > 0u);

    for (uint32_t s = 1u; s <= NUM_STREAMS; s++) {
        size_t idx = (size_t)(s - 1u);
        for (;;) {
            size_t got = zb_stream_pop(&tbl, s, drain, sizeof(drain));
            if (got == 0u) {
                break;
            }
            assert(actual_len[idx] + got < sizeof(actual[idx]));
            memcpy(actual[idx] + actual_len[idx], drain, got);
            actual_len[idx] += got;
        }
        actual[idx][actual_len[idx]] = '\0';
        expected[idx][expected_len[idx]] = '\0';

        const zb_stream_entry_t *entry = zb_stream_get(&tbl, s);
        assert(entry != NULL);
        assert(entry->frames_in == expected_frames[idx]);
        assert(entry->bytes_in == expected_len[idx]);
        assert(entry->pending_bytes == 0u);
        assert(actual_len[idx] == expected_len[idx]);
        assert(strcmp(actual[idx], expected[idx]) == 0);
    }

    printf("demux_stress_test passed! frames=%u bp_events=%u b1=%zu b2=%zu b3=%zu b4=%zu\n",
           decoded_total, bp_events,
           actual_len[0], actual_len[1], actual_len[2], actual_len[3]);
    return 0;
}
