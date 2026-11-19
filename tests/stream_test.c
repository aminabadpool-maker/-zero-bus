#include "zb_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    zb_stream_table_t tbl;
    zb_stream_table_init(&tbl);

    uint8_t b1[32], b2[32];
    assert(zb_stream_open(&tbl, 1, b1, sizeof(b1), 16) == ZB_STREAM_OK);
    assert(zb_stream_open(&tbl, 2, b2, sizeof(b2), 16) == ZB_STREAM_OK);

    const uint8_t chunk1[] = "1234567890";
    assert(zb_stream_push(&tbl, 1, chunk1, 10) == ZB_STREAM_OK);
    /* Pushing 8 more exceeds high_watermark=16 -> backpressure */
    assert(zb_stream_push(&tbl, 1, chunk1, 8) == ZB_STREAM_ERR_BACKPRESSURE);

    uint8_t out[16];
    size_t got = zb_stream_pop(&tbl, 1, out, 6);
    assert(got == 6);
    assert(memcmp(out, "123456", 6) == 0);

    /* Now 4 bytes left in stream 1; pushing 8 fits (4 + 8 = 12 <= 16) */
    assert(zb_stream_push(&tbl, 1, chunk1, 8) == ZB_STREAM_OK);

    const zb_stream_entry_t *e1 = zb_stream_get(&tbl, 1);
    assert(e1 != NULL);
    assert(e1->frames_in == 2);
    assert(e1->bytes_in == 18);
    assert(e1->pending_bytes == 12);

    printf("stream_test passed! stream1 frames_in=%llu pending=%zu\n",
           (unsigned long long)e1->frames_in, e1->pending_bytes);
    return 0;
}
