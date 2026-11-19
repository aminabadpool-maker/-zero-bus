#include "zb_transport.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
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

static int fill_sockaddr_un(const char *sock_path, struct sockaddr_un *addr, socklen_t *out_len) {
    if (!sock_path || !addr || !out_len || sock_path[0] == '\0') {
        return -1;
    }
    size_t path_len = strlen(sock_path);
    if (path_len >= sizeof(addr->sun_path)) {
        return -1;
    }
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    memcpy(addr->sun_path, sock_path, path_len + 1u);
    *out_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1u);
    return 0;
}

int zb_unix_listen(const char *sock_path, int backlog) {
    if (!sock_path || backlog <= 0) {
        return -1;
    }
    struct sockaddr_un addr;
    socklen_t addr_len = 0;
    if (fill_sockaddr_un(sock_path, &addr, &addr_len) != 0) {
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    unlink(sock_path);

    if (bind(fd, (const struct sockaddr *)&addr, addr_len) != 0) {
        zb_close_fd(&fd);
        return -1;
    }
    if (listen(fd, backlog) != 0) {
        zb_close_fd(&fd);
        unlink(sock_path);
        return -1;
    }
    return fd;
}

int zb_unix_accept(int listen_fd) {
    if (listen_fd < 0) {
        return -1;
    }
    for (;;) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd >= 0) {
            return client_fd;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
}

int zb_unix_connect(const char *sock_path) {
    if (!sock_path) {
        return -1;
    }
    struct sockaddr_un addr;
    socklen_t addr_len = 0;
    if (fill_sockaddr_un(sock_path, &addr, &addr_len) != 0) {
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    for (;;) {
        if (connect(fd, (const struct sockaddr *)&addr, addr_len) == 0) {
            return fd;
        }
        if (errno == EINTR) {
            continue;
        }
        zb_close_fd(&fd);
        return -1;
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
