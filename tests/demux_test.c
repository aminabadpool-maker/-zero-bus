#include "zero_bus.h"
#include "zb_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    zb_stream_table_t tbl;
    zb_stream_table_init(&tbl);

    uint8_t s1_ring[32];
    uint8_t s2_ring[32];
    assert(zb_stream_open(&tbl, 1, s1_ring, sizeof(s1_ring), 16) == ZB_STREAM_OK);
    assert(zb_stream_open(&tbl, 2, s2_ring, sizeof(s2_ring), 24) == ZB_STREAM_OK);

    /* Build wire stream with three frames:
     * Frame A: stream=1, payload="0123456789" (10 bytes -> fits in watermark 16)
     * Frame B: stream=2, payload="abc-stream-2" (12 bytes -> fits in watermark 24)
     * Frame C: stream=1, payload="ABCDEFGH" (8 bytes -> 10 + 8 = 18 > 16 -> backpressure)
     */
    uint8_t wire[512];
    size_t wire_used = 0;

    const char *p1 = "0123456789";
    size_t n1 = zb_frame_encode(wire + wire_used, sizeof(wire) - wire_used,
                                10, 1, (const uint8_t *)p1, (uint32_t)strlen(p1));
    assert(n1 > 0);
    wire_used += n1;

    const char *p2 = "abc-stream-2";
    size_t n2 = zb_frame_encode(wire + wire_used, sizeof(wire) - wire_used,
                                11, 2, (const uint8_t *)p2, (uint32_t)strlen(p2));
    assert(n2 > 0);
    wire_used += n2;

    const char *p3 = "ABCDEFGH";
    size_t n3 = zb_frame_encode(wire + wire_used, sizeof(wire) - wire_used,
                                12, 1, (const uint8_t *)p3, (uint32_t)strlen(p3));
    assert(n3 > 0);
    wire_used += n3;

    uint8_t rx_backing[ZB_RX_BUF_CAP];
    zb_rx_stream_t rx;
    assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);

    zb_hdr_t hdr;
    uint8_t payload_buf[128];
    int bp_seen = 0;
    int s2_frames = 0;

    size_t off = 0;
    while (off < wire_used) {
        size_t chunk = 5;
        if (chunk > wire_used - off) {
            chunk = wire_used - off;
        }
        assert(zb_rx_push(&rx, wire + off, chunk) == ZB_OK);
        off += chunk;

        for (;;) {
            zb_status_t st = zb_rx_pop_frame(&rx, &hdr, payload_buf, sizeof(payload_buf));
            if (st == ZB_ERR_NEED_MORE) {
                break;
            }
            assert(st == ZB_OK);

            zb_stream_status_t push_st = zb_stream_push(&tbl, hdr.stream_id,
                                                        payload_buf, hdr.payload_len);
            if (hdr.stream_id == 1 && push_st == ZB_STREAM_ERR_BACKPRESSURE) {
                bp_seen++;
                /* Drain 6 bytes from stream 1 so 4 bytes remain; 4 + 8 = 12 <= 16 */
                uint8_t drained[8];
                size_t got = zb_stream_pop(&tbl, 1, drained, 6);
                assert(got == 6);
                assert(memcmp(drained, "012345", 6) == 0);

                push_st = zb_stream_push(&tbl, hdr.stream_id,
                                           payload_buf, hdr.payload_len);
                assert(push_st == ZB_STREAM_OK);
            } else {
                assert(push_st == ZB_STREAM_OK);
                if (hdr.stream_id == 2) {
                    s2_frames++;
                }
            }
        }
    }

    assert(bp_seen == 1);
    assert(s2_frames == 1);

    const zb_stream_entry_t *e1 = zb_stream_get(&tbl, 1);
    const zb_stream_entry_t *e2 = zb_stream_get(&tbl, 2);
    assert(e1 != NULL && e2 != NULL);
    assert(e1->frames_in == 2);
    assert(e1->bytes_in == 18);
    assert(e1->pending_bytes == 12);
    assert(e2->frames_in == 1);
    assert(e2->bytes_in == 12);
    assert(e2->pending_bytes == 12);

    uint8_t s2_out[32];
    size_t s2_got = zb_stream_pop(&tbl, 2, s2_out, sizeof(s2_out));
    assert(s2_got == strlen("abc-stream-2"));
    assert(memcmp(s2_out, "abc-stream-2", s2_got) == 0);

    printf("demux_test passed! bp_seen=%d s1_pending=%zu s2_popped=%zu\n",
           bp_seen, e1->pending_bytes, s2_got);
    return 0;
}
