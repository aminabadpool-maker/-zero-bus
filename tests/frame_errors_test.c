#include "zero_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void reset_rx(zb_rx_stream_t *rx, uint8_t *backing, size_t cap) {
    assert(zb_rx_init(rx, backing, cap) == ZB_OK);
}

int main(void) {
    uint8_t rx_backing[ZB_RX_BUF_CAP];
    zb_rx_stream_t rx;
    zb_hdr_t out_hdr;
    uint8_t out_payload[128];

    const char *msg = "payload-integrity-check";
    uint8_t wire[256];
    size_t wire_len = zb_frame_encode(wire, sizeof(wire), 1, 1,
                                      (const uint8_t *)msg, (uint32_t)strlen(msg));
    assert(wire_len == sizeof(zb_hdr_t) + strlen(msg));

    /* 1. Bad magic */
    uint8_t bad_magic_buf[256];
    memcpy(bad_magic_buf, wire, wire_len);
    bad_magic_buf[0] ^= 0xFFu;
    reset_rx(&rx, rx_backing, sizeof(rx_backing));
    assert(zb_rx_push(&rx, bad_magic_buf, wire_len) == ZB_OK);
    assert(zb_rx_pop_frame(&rx, &out_hdr, out_payload, sizeof(out_payload)) == ZB_ERR_BAD_MAGIC);

    /* 2. Bad version */
    uint8_t bad_ver_buf[256];
    memcpy(bad_ver_buf, wire, wire_len);
    zb_hdr_t ver_hdr;
    memcpy(&ver_hdr, bad_ver_buf, sizeof(ver_hdr));
    ver_hdr.version = (uint16_t)(ZB_VERSION + 1u);
    memcpy(bad_ver_buf, &ver_hdr, sizeof(ver_hdr));
    reset_rx(&rx, rx_backing, sizeof(rx_backing));
    assert(zb_rx_push(&rx, bad_ver_buf, wire_len) == ZB_OK);
    assert(zb_rx_pop_frame(&rx, &out_hdr, out_payload, sizeof(out_payload)) == ZB_ERR_BAD_VERSION);

    /* 3. Payload length larger than ZB_MAX_PAYLOAD */
    zb_hdr_t ovf_hdr;
    memset(&ovf_hdr, 0, sizeof(ovf_hdr));
    ovf_hdr.magic = ZB_MAGIC;
    ovf_hdr.version = (uint16_t)ZB_VERSION;
    ovf_hdr.msg_type = 1;
    ovf_hdr.stream_id = 2;
    ovf_hdr.payload_len = ZB_MAX_PAYLOAD + 1u;
    ovf_hdr.crc32 = 0;
    reset_rx(&rx, rx_backing, sizeof(rx_backing));
    assert(zb_rx_push(&rx, (const uint8_t *)&ovf_hdr, sizeof(ovf_hdr)) == ZB_OK);
    assert(zb_rx_pop_frame(&rx, &out_hdr, out_payload, sizeof(out_payload)) == ZB_ERR_OVERFLOW);

    /* 4. Corrupted CRC32 / payload */
    uint8_t bad_crc_buf[256];
    memcpy(bad_crc_buf, wire, wire_len);
    bad_crc_buf[wire_len - 1u] ^= 0x01u;
    reset_rx(&rx, rx_backing, sizeof(rx_backing));
    assert(zb_rx_push(&rx, bad_crc_buf, wire_len) == ZB_OK);
    assert(zb_rx_pop_frame(&rx, &out_hdr, out_payload, sizeof(out_payload)) == ZB_ERR_BAD_CRC);

    printf("frame_errors_test passed!\n");
    return 0;
}
