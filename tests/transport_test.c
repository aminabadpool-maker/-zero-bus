#include "zb_transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    assert(zb_ignore_sigpipe() == 0);

    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    uint8_t tx_buf[4096];
    const char *p1 = "frame-one-over-unix-socket";
    const char *p2 = "frame-two-payload";

    assert(zb_send_frame(sv[0], 101, 7, (const uint8_t *)p1,
                         (uint32_t)strlen(p1), tx_buf, sizeof(tx_buf)) == ZB_IO_OK);
    assert(zb_send_frame(sv[0], 102, 8, (const uint8_t *)p2,
                         (uint32_t)strlen(p2), tx_buf, sizeof(tx_buf)) == ZB_IO_OK);
    zb_close_fd(&sv[0]);

    uint8_t rx_backing[4096];
    zb_rx_stream_t rx;
    assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);

    zb_hdr_t hdr;
    uint8_t payload[256];
    int count = 0;

    for (;;) {
        zb_io_status_t st = zb_recv_next_frame(sv[1], &rx, &hdr, payload, sizeof(payload));
        if (st == ZB_IO_OK) {
            count++;
            printf("Recv #%d: stream=%u type=%u len=%u data=%.*s\n",
                   count, hdr.stream_id, hdr.msg_type, hdr.payload_len,
                   (int)hdr.payload_len, (const char *)payload);
        } else if (st == ZB_IO_EOF) {
            break;
        } else {
            fprintf(stderr, "Transport error: %d\n", st);
            zb_close_fd(&sv[1]);
            return 1;
        }
    }

    zb_close_fd(&sv[1]);
    assert(count == 2);
    printf("transport_test passed!\n");
    return 0;
}
