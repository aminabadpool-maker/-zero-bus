#include "zb_transport.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define STRESS_FRAME_COUNT 16u

int main(void) {
    assert(zb_ignore_sigpipe() == 0);

    const char *tmp = getenv("TMPDIR");
    if (!tmp || tmp[0] == '\0') {
        tmp = "/tmp";
    }

    char sock_path[256];
    int p_len = snprintf(sock_path, sizeof(sock_path), "%s/zb_stress_%ld.sock",
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

        uint8_t batch[4096];
        size_t batch_len = 0;

        for (uint32_t i = 0; i < STRESS_FRAME_COUNT; i++) {
            char msg[64];
            int mlen = snprintf(msg, sizeof(msg), "seq-%02u-data-packet", i);
            assert(mlen > 0);
            uint16_t mtype = (uint16_t)(200u + i);
            uint32_t sid = (i % 3u) + 1u;

            size_t flen = zb_frame_encode(batch + batch_len, sizeof(batch) - batch_len,
                                          mtype, sid, (const uint8_t *)msg, (uint32_t)mlen);
            if (flen == 0) {
                zb_close_fd(&cli_fd);
                _exit(3);
            }
            batch_len += flen;
        }

        size_t off = 0;
        while (off < batch_len) {
            size_t chunk = 7u;
            if (chunk > batch_len - off) {
                chunk = batch_len - off;
            }
            size_t written = 0;
            zb_io_status_t wst = zb_write_all(cli_fd, batch + off, chunk, &written);
            if (wst != ZB_IO_OK || written != chunk) {
                zb_close_fd(&cli_fd);
                _exit(4);
            }
            off += written;
        }

        zb_close_fd(&cli_fd);
        _exit(0);
    }

    int conn_fd = zb_unix_accept(listen_fd);
    assert(conn_fd >= 0);

    /* Intentionally small rx buffer to exercise incremental reads and compaction */
    uint8_t rx_backing[64];
    zb_rx_stream_t rx;
    assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);

    for (uint32_t i = 0; i < STRESS_FRAME_COUNT; i++) {
        char expected_msg[64];
        int expected_len = snprintf(expected_msg, sizeof(expected_msg),
                                    "seq-%02u-data-packet", i);
        assert(expected_len > 0);
        uint16_t expected_type = (uint16_t)(200u + i);
        uint32_t expected_sid = (i % 3u) + 1u;

        zb_hdr_t hdr;
        uint8_t payload[128];
        zb_io_status_t rst = zb_recv_next_frame(conn_fd, &rx, &hdr, payload, sizeof(payload));
        assert(rst == ZB_IO_OK);
        assert(hdr.magic == ZB_MAGIC);
        assert(hdr.version == (uint16_t)ZB_VERSION);
        assert(hdr.msg_type == expected_type);
        assert(hdr.stream_id == expected_sid);
        assert(hdr.payload_len == (uint32_t)expected_len);
        assert(memcmp(payload, expected_msg, (size_t)expected_len) == 0);
    }

    zb_hdr_t extra_hdr;
    uint8_t extra_payload[16];
    assert(zb_recv_next_frame(conn_fd, &rx, &extra_hdr, extra_payload, sizeof(extra_payload)) == ZB_IO_EOF);

    zb_close_fd(&conn_fd);
    zb_close_fd(&listen_fd);
    unlink(sock_path);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    printf("unix_sock_stress_test passed! frames=%u\n", STRESS_FRAME_COUNT);
    return 0;
}
