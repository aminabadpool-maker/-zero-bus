#include "zb_transport.h"
#include "zb_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    uint16_t msg_type;
    uint32_t stream_id;
    const char *text;
} tx_spec_t;

int main(void) {
    assert(zb_ignore_sigpipe() == 0);

    const char *tmp = getenv("TMPDIR");
    if (!tmp || tmp[0] == '\0') {
        tmp = "/tmp";
    }

    char sock_path[256];
    int p_len = snprintf(sock_path, sizeof(sock_path), "%s/zb_demux_bp_%ld.sock",
                         tmp, (long)getpid());
    assert(p_len > 0 && (size_t)p_len < sizeof(sock_path));

    int listen_fd = zb_unix_listen(sock_path, 4);
    assert(listen_fd >= 0);

    const tx_spec_t specs[] = {
        { 10u, 1u, "s1-part-A-" },      /* 10 bytes -> fits in watermark 16 */
        { 11u, 2u, "s2-alpha-" },       /*  9 bytes -> fits in watermark 32 */
        { 12u, 1u, "s1-part-B!" },      /* 10 bytes -> 10 + 10 = 20 > 16 -> BP */
        { 13u, 3u, "s3-stream-ok" },    /* 12 bytes -> fits in watermark 32 */
        { 14u, 2u, "s2-beta-end" }      /* 11 bytes -> 9 + 11 = 20 <= 32 */
    };
    const size_t spec_count = sizeof(specs) / sizeof(specs[0]);

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

        uint8_t batch[2048];
        size_t batch_len = 0;

        for (size_t i = 0; i < spec_count; i++) {
            size_t tlen = strlen(specs[i].text);
            size_t flen = zb_frame_encode(batch + batch_len, sizeof(batch) - batch_len,
                                          specs[i].msg_type, specs[i].stream_id,
                                          (const uint8_t *)specs[i].text, (uint32_t)tlen);
            if (flen == 0) {
                zb_close_fd(&cli_fd);
                _exit(3);
            }
            batch_len += flen;
        }

        size_t off = 0;
        while (off < batch_len) {
            size_t chunk = 9u;
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

    zb_stream_table_t tbl;
    zb_stream_table_init(&tbl);

    uint8_t r1[32], r2[64], r3[64];
    assert(zb_stream_open(&tbl, 1u, r1, sizeof(r1), 16u) == ZB_STREAM_OK);
    assert(zb_stream_open(&tbl, 2u, r2, sizeof(r2), 32u) == ZB_STREAM_OK);
    assert(zb_stream_open(&tbl, 3u, r3, sizeof(r3), 32u) == ZB_STREAM_OK);

    uint8_t rx_backing[128];
    zb_rx_stream_t rx;
    assert(zb_rx_init(&rx, rx_backing, sizeof(rx_backing)) == ZB_OK);

    char s1_collected[64];
    size_t s1_col_len = 0;
    int bp_count = 0;

    for (size_t i = 0; i < spec_count; i++) {
        zb_hdr_t hdr;
        uint8_t payload[128];
        zb_io_status_t rst = zb_recv_next_frame(conn_fd, &rx, &hdr, payload, sizeof(payload));
        assert(rst == ZB_IO_OK);
        assert(hdr.magic == ZB_MAGIC);
        assert(hdr.version == (uint16_t)ZB_VERSION);
        assert(hdr.msg_type == specs[i].msg_type);
        assert(hdr.stream_id == specs[i].stream_id);
        assert(hdr.payload_len == (uint32_t)strlen(specs[i].text));
        assert(memcmp(payload, specs[i].text, hdr.payload_len) == 0);

        zb_stream_status_t pst = zb_stream_push(&tbl, hdr.stream_id, payload, hdr.payload_len);
        if (hdr.stream_id == 1u && pst == ZB_STREAM_ERR_BACKPRESSURE) {
            bp_count++;
            /* Drain 6 bytes from stream 1 so 4 bytes remain; 4 + 10 = 14 <= 16 */
            uint8_t tmp_out[16];
            size_t popped = zb_stream_pop(&tbl, 1u, tmp_out, 6u);
            assert(popped == 6u);
            memcpy(s1_collected + s1_col_len, tmp_out, popped);
            s1_col_len += popped;

            pst = zb_stream_push(&tbl, hdr.stream_id, payload, hdr.payload_len);
            assert(pst == ZB_STREAM_OK);
        } else {
            assert(pst == ZB_STREAM_OK);
        }
    }

    zb_hdr_t eof_hdr;
    uint8_t eof_payload[16];
    assert(zb_recv_next_frame(conn_fd, &rx, &eof_hdr, eof_payload, sizeof(eof_payload)) == ZB_IO_EOF);

    /* Drain remaining bytes from stream 1 */
    uint8_t rem1[32];
    size_t n_rem1 = zb_stream_pop(&tbl, 1u, rem1, sizeof(rem1));
    assert(n_rem1 == 14u);
    memcpy(s1_collected + s1_col_len, rem1, n_rem1);
    s1_col_len += n_rem1;
    s1_collected[s1_col_len] = '\0';
    assert(strcmp(s1_collected, "s1-part-A-s1-part-B!") == 0);

    /* Drain and verify stream 2 */
    char s2_buf[64];
    size_t n2 = zb_stream_pop(&tbl, 2u, (uint8_t *)s2_buf, sizeof(s2_buf) - 1u);
    assert(n2 == strlen("s2-alpha-s2-beta-end"));
    s2_buf[n2] = '\0';
    assert(strcmp(s2_buf, "s2-alpha-s2-beta-end") == 0);

    /* Drain and verify stream 3 */
    char s3_buf[64];
    size_t n3 = zb_stream_pop(&tbl, 3u, (uint8_t *)s3_buf, sizeof(s3_buf) - 1u);
    assert(n3 == strlen("s3-stream-ok"));
    s3_buf[n3] = '\0';
    assert(strcmp(s3_buf, "s3-stream-ok") == 0);

    assert(bp_count == 1);

    zb_close_fd(&conn_fd);
    zb_close_fd(&listen_fd);
    unlink(sock_path);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    printf("unix_sock_demux_bp_test passed! bp=%d s1=%s s2=%s s3=%s\n",
           bp_count, s1_collected, s2_buf, s3_buf);
    return 0;
}
