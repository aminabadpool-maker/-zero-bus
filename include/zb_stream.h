#ifndef ZB_STREAM_H
#define ZB_STREAM_H

#include "zb_mem.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZB_MAX_STREAMS 32u

typedef enum {
    ZB_STREAM_OK = 0,
    ZB_STREAM_ERR_ARG = -1,
    ZB_STREAM_ERR_FULL_TABLE = -2,
    ZB_STREAM_ERR_NOT_FOUND = -3,
    ZB_STREAM_ERR_BACKPRESSURE = -4,
    ZB_STREAM_ERR_CLOSED = -5
} zb_stream_status_t;

typedef struct {
    uint32_t stream_id;
    bool active;
    bool closed;
    size_t high_watermark;
    size_t pending_bytes;
    uint64_t frames_in;
    uint64_t bytes_in;
    zb_ring_t ring;
} zb_stream_entry_t;

typedef struct {
    zb_stream_entry_t entries[ZB_MAX_STREAMS];
    size_t count;
} zb_stream_table_t;

void zb_stream_table_init(zb_stream_table_t *tbl);
zb_stream_status_t zb_stream_open(zb_stream_table_t *tbl, uint32_t stream_id,
                                  uint8_t *ring_backing, size_t ring_cap,
                                  size_t high_watermark);
zb_stream_status_t zb_stream_close(zb_stream_table_t *tbl, uint32_t stream_id);
zb_stream_status_t zb_stream_push(zb_stream_table_t *tbl, uint32_t stream_id,
                                 const uint8_t *data, size_t len);
size_t zb_stream_pop(zb_stream_table_t *tbl, uint32_t stream_id,
                     uint8_t *dst, size_t len);
const zb_stream_entry_t *zb_stream_get(const zb_stream_table_t *tbl, uint32_t stream_id);

#endif
