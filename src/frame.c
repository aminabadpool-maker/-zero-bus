#include "zero_bus.h"
#include <string.h>

uint32_t zb_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

size_t zb_frame_encode(uint8_t *out, size_t out_cap,
                       uint16_t msg_type, uint32_t stream_id,
                       const uint8_t *payload, uint32_t payload_len) {
    if (!out || payload_len > ZB_MAX_PAYLOAD) {
        return 0;
    }
    if (payload_len > 0 && !payload) {
        return 0;
    }
    size_t total = sizeof(zb_hdr_t) + (size_t)payload_len;
    if (out_cap < total) {
        return 0;
    }

    zb_hdr_t hdr;
    hdr.magic = ZB_MAGIC;
    hdr.version = (uint16_t)ZB_VERSION;
    hdr.msg_type = msg_type;
    hdr.stream_id = stream_id;
    hdr.payload_len = payload_len;
    hdr.crc32 = (payload_len > 0) ? zb_crc32(payload, payload_len)
                                  : zb_crc32((const uint8_t *)"", 0);

    memcpy(out, &hdr, sizeof(hdr));
    if (payload_len > 0) {
        memcpy(out + sizeof(hdr), payload, payload_len);
    }
    return total;
}

zb_status_t zb_rx_init(zb_rx_stream_t *rx, uint8_t *backing_buf, size_t cap) {
    if (!rx || !backing_buf || cap < sizeof(zb_hdr_t)) {
        return ZB_ERR_ARG;
    }
    rx->buf = backing_buf;
    rx->cap = cap;
    rx->len = 0;
    return ZB_OK;
}

zb_status_t zb_rx_push(zb_rx_stream_t *rx, const uint8_t *chunk, size_t chunk_len) {
    if (!rx || !rx->buf) {
        return ZB_ERR_ARG;
    }
    if (chunk_len == 0) {
        return ZB_OK;
    }
    if (!chunk) {
        return ZB_ERR_ARG;
    }
    if (chunk_len > rx->cap - rx->len) {
        return ZB_ERR_OVERFLOW;
    }
    memcpy(rx->buf + rx->len, chunk, chunk_len);
    rx->len += chunk_len;
    return ZB_OK;
}

zb_status_t zb_rx_pop_frame(zb_rx_stream_t *rx, zb_hdr_t *out_hdr,
                            uint8_t *out_payload, size_t out_payload_cap) {
    if (!rx || !rx->buf || !out_hdr) {
        return ZB_ERR_ARG;
    }
    if (rx->len < sizeof(zb_hdr_t)) {
        return ZB_ERR_NEED_MORE;
    }

    zb_hdr_t hdr;
    memcpy(&hdr, rx->buf, sizeof(hdr));

    if (hdr.magic != ZB_MAGIC) {
        return ZB_ERR_BAD_MAGIC;
    }
    if (hdr.version != ZB_VERSION) {
        return ZB_ERR_BAD_VERSION;
    }
    if (hdr.payload_len > ZB_MAX_PAYLOAD) {
        return ZB_ERR_OVERFLOW;
    }

    size_t frame_total = sizeof(zb_hdr_t) + (size_t)hdr.payload_len;
    if (frame_total > rx->cap) {
        return ZB_ERR_OVERFLOW;
    }
    if (rx->len < frame_total) {
        return ZB_ERR_NEED_MORE;
    }

    if (hdr.payload_len > 0) {
        if (!out_payload || out_payload_cap < (size_t)hdr.payload_len) {
            return ZB_ERR_OVERFLOW;
        }
    }

    const uint8_t *payload_ptr = rx->buf + sizeof(zb_hdr_t);
    uint32_t actual_crc = zb_crc32(payload_ptr, hdr.payload_len);
    if (actual_crc != hdr.crc32) {
        return ZB_ERR_BAD_CRC;
    }

    *out_hdr = hdr;
    if (hdr.payload_len > 0) {
        memcpy(out_payload, payload_ptr, hdr.payload_len);
    }

    size_t rem = rx->len - frame_total;
    if (rem > 0) {
        memmove(rx->buf, rx->buf + frame_total, rem);
    }
    rx->len = rem;
    return ZB_OK;
}
