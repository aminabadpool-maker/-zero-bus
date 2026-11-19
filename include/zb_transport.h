#ifndef ZB_TRANSPORT_H
#define ZB_TRANSPORT_H

#include "zero_bus.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ZB_IO_OK = 0,
    ZB_IO_EOF = 1,
    ZB_IO_ERR = -1,
    ZB_IO_BAD_ARG = -2
} zb_io_status_t;

int zb_ignore_sigpipe(void);
void zb_close_fd(int *fd);

zb_io_status_t zb_write_all(int fd, const uint8_t *buf, size_t len, size_t *out_written);
zb_io_status_t zb_send_frame(int fd, uint16_t msg_type, uint32_t stream_id,
                             const uint8_t *payload, uint32_t payload_len,
                             uint8_t *scratch, size_t scratch_cap);
zb_io_status_t zb_recv_next_frame(int fd, zb_rx_stream_t *rx,
                                  zb_hdr_t *out_hdr,
                                  uint8_t *out_payload, size_t out_payload_cap);

#endif
