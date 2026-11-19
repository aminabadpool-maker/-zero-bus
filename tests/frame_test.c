#include "zero_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *msg1 = "hello-zero-bus";
    const char *msg2 = "stream-2-payload";
    uint8_t wire[512];

    size_t n1 = zb_frame_encode(wire, sizeof(wire), 1, 10,
                                (const uint8_t *)msg1, (uint32_t)strlen(msg1));
    assert(n1 > sizeof(zb_hdr_t));

    size_t n2 = zb_frame_encode(wire + n1, sizeof(wire) - n1, 2, 20,
                                (const uint8_t *)msg2, (uint32_t)strlen(msg2));
    assert(n2 > sizeof(zb_hdr_t));

    size_t total = n1 + n2;
    uint8_t rx_mem[1024];
    zb_rx_stream_t rx;
    assert(zb_rx_init(&rx, rx_mem, sizeof(rx_mem)) == ZB_OK);

    zb_hdr_t hdr;
    uint8_t payload[256];
    int decoded = 0;

    for (size_t off = 0; off < total; off += 3) {
        size_t step = (total - off < 3) ? (total - off) : 3;
        assert(zb_rx_push(&rx, wire + off, step) == ZB_OK);

        for (;;) {
            zb_status_t st = zb_rx_pop_frame(&rx, &hdr, payload, sizeof(payload));
            if (st == ZB_OK) {
                decoded++;
                printf("Decoded frame #%d: stream=%u type=%u len=%u data=%.*s\n",
                       decoded, hdr.stream_id, hdr.msg_type, hdr.payload_len,
                       (int)hdr.payload_len, (const char *)payload);
            } else if (st == ZB_ERR_NEED_MORE) {
                break;
            } else {
                fprintf(stderr, "Unexpected status: %d\n", st);
                return 1;
            }
        }
    }

    assert(decoded == 2);
    printf("frame_test passed!\n");
    return 0;
}
