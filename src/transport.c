#include "zb_transport.h"
#include <errno.h>
#include <signal.h>
#include <unistd.h>

int zb_ignore_sigpipe(void) {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa, NULL) != 0) {
        return -1;
    }
    return 0;
}

void zb_close_fd(int *fd) {
    if (!fd || *fd < 0) {
        return;
    }
    int target = *fd;
    *fd = -1;
    for (;;) {
        if (close(target) == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }
}

zb_io_status_t zb_write_all(int fd, const uint8_t *buf, size_t len, size_t *out_written) {
    if (out_written) {
        *out_written = 0;
    }
    if (fd < 0 || (len > 0 && !buf)) {
        return ZB_IO_BAD_ARG;
    }

    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, buf + total, len - total);
        if (n > 0) {
            total += (size_t)n;
            if (out_written) {
                *out_written = total;
            }
            continue;
        }
        if (n == 0) {
            return ZB_IO_EOF;
        }
        if (errno == EINTR) {
            continue;
        }
        return ZB_IO_ERR;
    }
    return ZB_IO_OK;
}

zb_io_status_t zb_send_frame(int fd, uint16_t msg_type, uint32_t stream_id,
                             const uint8_t *payload, uint32_t payload_len,
                             uint8_t *scratch, size_t scratch_cap) {
    if (fd < 0 || !scratch || scratch_cap < sizeof(zb_hdr_t)) {
        return ZB_IO_BAD_ARG;
    }
    size_t wire_len = zb_frame_encode(scratch, scratch_cap, msg_type, stream_id,
                                      payload, payload_len);
    if (wire_len == 0) {
        return ZB_IO_BAD_ARG;
    }
    return zb_write_all(fd, scratch, wire_len, NULL);
}

zb_io_status_t zb_recv_next_frame(int fd, zb_rx_stream_t *rx,
                                  zb_hdr_t *out_hdr,
                                  uint8_t *out_payload, size_t out_payload_cap) {
    if (fd < 0 || !rx || !rx->buf || !out_hdr) {
        return ZB_IO_BAD_ARG;
    }

    for (;;) {
        zb_status_t st = zb_rx_pop_frame(rx, out_hdr, out_payload, out_payload_cap);
        if (st == ZB_OK) {
            return ZB_IO_OK;
        }
        if (st != ZB_ERR_NEED_MORE) {
            return ZB_IO_ERR;
        }

        if (rx->len >= rx->cap) {
            return ZB_IO_ERR;
        }

        ssize_t n = read(fd, rx->buf + rx->len, rx->cap - rx->len);
        if (n > 0) {
            rx->len += (size_t)n;
            continue;
        }
        if (n == 0) {
            return (rx->len == 0) ? ZB_IO_EOF : ZB_IO_ERR;
        }
        if (errno == EINTR) {
            continue;
        }
        return ZB_IO_ERR;
    }
}
