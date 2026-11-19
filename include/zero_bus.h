#ifndef ZERO_BUS_H
#define ZERO_BUS_H

#include <stddef.h>
#include <stdint.h>

#define ZB_MAGIC 0x5A423031u
#define ZB_VERSION 1u
#define ZB_MAX_PAYLOAD (1024u * 1024u)
#define ZB_RX_BUF_CAP (2u * 1024u * 1024u)

typedef enum {
    ZB_OK = 0,
    ZB_ERR_ARG = -1,
    ZB_ERR_OVERFLOW = -2,
    ZB_ERR_BAD_MAGIC = -3,
    ZB_ERR_BAD_VERSION = -4,
    ZB_ERR_BAD_CRC = -5,
    ZB_ERR_NEED_MORE = -6
} zb_status_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t msg_type;
    uint32_t stream_id;
    uint32_t payload_len;
    uint32_t crc32;
} zb_hdr_t;

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
} zb_rx_stream_t;

uint32_t zb_crc32(const uint8_t *data, size_t len);
size_t zb_frame_encode(uint8_t *out, size_t out_cap,
                       uint16_t msg_type, uint32_t stream_id,
                       const uint8_t *payload, uint32_t payload_len);
zb_status_t zb_rx_init(zb_rx_stream_t *rx, uint8_t *backing_buf, size_t cap);
zb_status_t zb_rx_push(zb_rx_stream_t *rx, const uint8_t *chunk, size_t chunk_len);
zb_status_t zb_rx_pop_frame(zb_rx_stream_t *rx, zb_hdr_t *out_hdr,
                            uint8_t *out_payload, size_t out_payload_cap);

#endif
