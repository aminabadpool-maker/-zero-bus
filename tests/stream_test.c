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
    assert(tbl.count == 2);

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

    /* Close stream 1: clears buffer, frees slot, decrements count */
    assert(zb_stream_close(&tbl, 1) == ZB_STREAM_OK);
    assert(zb_stream_close(&tbl, 1) == ZB_STREAM_ERR_NOT_FOUND);
    assert(zb_stream_get(&tbl, 1) == NULL);
    assert(zb_stream_push(&tbl, 1, chunk1, 4) == ZB_STREAM_ERR_NOT_FOUND);
    assert(tbl.count == 1);

    /* Reuse freed slot for a brand new stream_id=99 */
    uint8_t b99[32];
    assert(zb_stream_open(&tbl, 99, b99, sizeof(b99), 16) == ZB_STREAM_OK);
    assert(tbl.count == 2);
    assert(zb_stream_push(&tbl, 99, (const uint8_t *)"fresh", 5) == ZB_STREAM_OK);
    const zb_stream_entry_t *e99 = zb_stream_get(&tbl, 99);
    assert(e99 != NULL);
    assert(e99->frames_in == 1);
    assert(e99->bytes_in == 5);
    assert(e99->pending_bytes == 5);
    got = zb_stream_pop(&tbl, 99, out, sizeof(out));
    assert(got == 5 && memcmp(out, "fresh", 5) == 0);
    assert(zb_stream_close(&tbl, 99) == ZB_STREAM_OK);
    assert(zb_stream_close(&tbl, 2) == ZB_STREAM_OK);
    assert(tbl.count == 0);

    printf("stream_test passed! slot_reuse=ok\n");
    return 0;
}
