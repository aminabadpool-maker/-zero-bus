#include "zero_bus.h"
#include <string.h>

int zb_validate_header(const uint8_t *buffer, size_t len, zb_header_t *out_header) {
    if (len < sizeof(zb_header_t)) {
        return -1;
    }
    const zb_header_t *hdr = (const zb_header_t *)buffer;
    if (hdr->magic != ZB_MAGIC) {
        return -2;
    }
    if (out_header) {
        *out_header = *hdr;
    }
    return 0;
}

size_t zb_serialize_packet(uint8_t *dest, uint8_t type, const uint8_t *payload, uint32_t payload_len) {
    zb_header_t *hdr = (zb_header_t *)dest;
    hdr->magic = ZB_MAGIC;
    hdr->version = 0x01;
    hdr->type = type;
    hdr->flags = 0x0000;
    hdr->payload_len = payload_len;
    hdr->checksum = 0x00000000;

    if (payload && payload_len > 0) {
        memcpy(dest + sizeof(zb_header_t), payload, payload_len);
    }
    return sizeof(zb_header_t) + payload_len;
}
