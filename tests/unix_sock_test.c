#include "zb_transport.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    assert(zb_ignore_sigpipe() == 0);

    const char *tmp = getenv("TMPDIR");
    if (!tmp || tmp[0] == '\0') {
        tmp = "/tmp";
    }

    char sock_path[256];
    int p_len = snprintf(sock_path, sizeof(sock_path), "%s/zb_unix_%ld.sock",
                         tmp, (long)getpid());
    assert(p_len > 0 && (size_t)p_len < sizeof(sock_path));

    int listen_fd = zb_unix_listen(sock_path, 4);
    assert(listen_fd >= 0);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        zb_close_fd(&listen_fd);
        int cli_fd = -1;
        for (int i = 0; i < 50; i++) {
            cli_fd = zb_unix_connect(sock_path);
            if (cli_fd >= 0) {
                break;
            }
            usleep(10000);
        }
        if (cli_fd < 0) {
            _exit(2);
        }

        uint8_t tx_buf[1024];
        const char *msg = "unix-domain-socket-ok";
        zb_io_status_t st = zb_send_frame(cli_fd, 55, 3, (const uint8_t *)msg,
                                          (uint32_t)strlen(msg), tx_buf, sizeof(tx_buf));
        zb_close_fd(&cli_fd);
        _exit(st == ZB_IO_OK ? 0 : 3);
    }

    int conn_fd = zb_unix_accept(listen_fd);
    assert(conn_fd >= 0);

    uint8_t rx_backing[2048];
    zb_rx_stream_t rx;
    assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);

    zb_hdr_t hdr;
    uint8_t payload[256];
    zb_io_status_t st = zb_recv_next_frame(conn_fd, &rx, &hdr, payload, sizeof(payload));
    assert(st == ZB_IO_OK);
    assert(hdr.stream_id == 3);
    assert(hdr.msg_type == 55);
    assert(hdr.payload_len == strlen("unix-domain-socket-ok"));
    assert(memcmp(payload, "unix-domain-socket-ok", hdr.payload_len) == 0);

    zb_close_fd(&conn_fd);
    zb_close_fd(&listen_fd);
    unlink(sock_path);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    printf("unix_sock_test passed!\n");
    return 0;
}
