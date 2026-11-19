#include "zero_bus.h"
#include "zb_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

int main(void) {
    const uint64_t SEED = 0x5A17C0DE2026ULL;
    uint64_t rng = SEED;

    zb_stream_table_t tbl;
    zb_stream_table_init(&tbl);

    uint8_t r1[64], r2[64], r3[64];
    assert(zb_stream_open(&tbl, 1, r1, sizeof(r1), 48) == ZB_STREAM_OK);
    assert(zb_stream_open(&tbl, 2, r2, sizeof(r2), 48) == ZB_STREAM_OK);
    assert(zb_stream_open(&tbl, 3, r3, sizeof(r3), 48) == ZB_STREAM_OK);

    uint8_t rx_backing[ZB_RX_BUF_CAP];
    zb_rx_stream_t rx;
    assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);

    zb_hdr_t hdr;
    uint8_t payload[256];
    uint8_t drain[128];

    uint64_t valid_frames = 0;
    uint64_t err_frames = 0;
    uint64_t need_more_hits = 0;
    uint64_t bp_hits = 0;

    for (uint32_t iter = 0; iter < 5000u; iter++) {
        uint64_t r = rng_next(&rng);
        uint8_t chunk[64];
        size_t chunk_len = (size_t)(r % sizeof(chunk)) + 1u;

        if ((iter % 137u) == 0u) {
            /* Inject a valid frame occasionally into the stream */
            uint32_t sid = (uint32_t)((rng_next(&rng) % 3u) + 1u);
            uint16_t mtype = (uint16_t)((rng_next(&rng) % 50u) + 1u);
            uint32_t plen = (uint32_t)((rng_next(&rng) % 24u) + 1u);
            uint8_t msg[24];
            for (uint32_t i = 0; i < plen; i++) {
                msg[i] = (uint8_t)(rng_next(&rng) & 0xFFu);
            }
            uint8_t wire[128];
            size_t wlen = zb_frame_encode(wire, sizeof(wire), mtype, sid, msg, plen);
            assert(wlen > 0);
            if (zb_rx_push(&rx, wire, wlen) != ZB_OK) {
                assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);
                assert(zb_rx_push(&rx, wire, wlen) == ZB_OK);
            }
        } else {
            for (size_t i = 0; i < chunk_len; i++) {
                chunk[i] = (uint8_t)(rng_next(&rng) & 0xFFu);
            }
            if (zb_rx_push(&rx, chunk, chunk_len) != ZB_OK) {
                assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);
                assert(zb_rx_push(&rx, chunk, chunk_len) == ZB_OK);
            }
        }

        for (;;) {
            zb_status_t st = zb_rx_pop_frame(&rx, &hdr, payload, sizeof(payload));
            if (st == ZB_ERR_NEED_MORE) {
                need_more_hits++;
                break;
            }
            if (st == ZB_OK) {
                assert(hdr.magic == ZB_MAGIC);
                assert(hdr.version == (uint16_t)ZB_VERSION);
                assert(hdr.payload_len <= ZB_MAX_PAYLOAD);
                assert(hdr.payload_len <= sizeof(payload));
                valid_frames++;

                if (hdr.stream_id >= 1u && hdr.stream_id <= 3u) {
                    zb_stream_status_t pst = zb_stream_push(&tbl, hdr.stream_id,
                                                            payload, hdr.payload_len);
                    if (pst == ZB_STREAM_ERR_BACKPRESSURE) {
                        bp_hits++;
                        (void)zb_stream_pop(&tbl, hdr.stream_id, drain, sizeof(drain));
                        pst = zb_stream_push(&tbl, hdr.stream_id, payload, hdr.payload_len);
                    }
                    assert(pst == ZB_STREAM_OK);
                }
            } else {
                err_frames++;
                /* After a framing/CRC/magic/overflow error, reset rx to resynchronize cleanly */
                assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);
                break;
            }
        }

        if ((iter % 25u) == 0u) {
            for (uint32_t s = 1u; s <= 3u; s++) {
                (void)zb_stream_pop(&tbl, s, drain, sizeof(drain));
            }
        }
    }

    assert(valid_frames > 0);
    assert(err_frames > 0);
    assert(need_more_hits > 0);

    printf("fuzz_test passed! valid=%llu err=%llu need_more=%llu bp=%llu\n",
           (unsigned long long)valid_frames,
           (unsigned long long)err_frames,
           (unsigned long long)need_more_hits,
           (unsigned long long)bp_hits);
    return 0;
}
