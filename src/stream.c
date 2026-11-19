#include "zb_stream.h"

void zb_stream_table_init(zb_stream_table_t *tbl) {
    if (!tbl) {
        return;
    }
    for (size_t i = 0; i < ZB_MAX_STREAMS; i++) {
        tbl->entries[i].stream_id = 0;
        tbl->entries[i].active = false;
        tbl->entries[i].closed = false;
        tbl->entries[i].high_watermark = 0;
        tbl->entries[i].pending_bytes = 0;
        tbl->entries[i].frames_in = 0;
        tbl->entries[i].bytes_in = 0;
        zb_ring_reset(&tbl->entries[i].ring);
    }
    tbl->count = 0;
}

static zb_stream_entry_t *find_entry(zb_stream_table_t *tbl, uint32_t stream_id) {
    if (!tbl) {
        return NULL;
    }
    for (size_t i = 0; i < ZB_MAX_STREAMS; i++) {
        if (tbl->entries[i].active && tbl->entries[i].stream_id == stream_id) {
            return &tbl->entries[i];
        }
    }
    return NULL;
}

zb_stream_status_t zb_stream_open(zb_stream_table_t *tbl, uint32_t stream_id,
                                  uint8_t *ring_backing, size_t ring_cap,
                                  size_t high_watermark) {
    if (!tbl || stream_id == 0 || !ring_backing || ring_cap == 0) {
        return ZB_STREAM_ERR_ARG;
    }
    if (high_watermark == 0 || high_watermark > ring_cap) {
        high_watermark = ring_cap;
    }

    zb_stream_entry_t *existing = find_entry(tbl, stream_id);
    if (existing) {
        existing->closed = false;
        existing->high_watermark = high_watermark;
        return ZB_STREAM_OK;
    }

    for (size_t i = 0; i < ZB_MAX_STREAMS; i++) {
        if (!tbl->entries[i].active) {
            if (zb_ring_init(&tbl->entries[i].ring, ring_backing, ring_cap) != ZB_MEM_OK) {
                return ZB_STREAM_ERR_ARG;
            }
            tbl->entries[i].stream_id = stream_id;
            tbl->entries[i].active = true;
            tbl->entries[i].closed = false;
            tbl->entries[i].high_watermark = high_watermark;
            tbl->entries[i].pending_bytes = 0;
            tbl->entries[i].frames_in = 0;
            tbl->entries[i].bytes_in = 0;
            tbl->count++;
            return ZB_STREAM_OK;
        }
    }
    return ZB_STREAM_ERR_FULL_TABLE;
}

zb_stream_status_t zb_stream_close(zb_stream_table_t *tbl, uint32_t stream_id) {
    zb_stream_entry_t *e = find_entry(tbl, stream_id);
    if (!e) {
        return ZB_STREAM_ERR_NOT_FOUND;
    }
    e->closed = true;
    return ZB_STREAM_OK;
}

zb_stream_status_t zb_stream_push(zb_stream_table_t *tbl, uint32_t stream_id,
                                 const uint8_t *data, size_t len) {
    if (!tbl || stream_id == 0 || (len > 0 && !data)) {
        return ZB_STREAM_ERR_ARG;
    }
    zb_stream_entry_t *e = find_entry(tbl, stream_id);
    if (!e) {
        return ZB_STREAM_ERR_NOT_FOUND;
    }
    if (e->closed) {
        return ZB_STREAM_ERR_CLOSED;
    }
    if (len > e->high_watermark || zb_ring_len(&e->ring) > e->high_watermark - len) {
        return ZB_STREAM_ERR_BACKPRESSURE;
    }

    size_t wrote = zb_ring_write(&e->ring, data, len);
    if (wrote < len) {
        return ZB_STREAM_ERR_BACKPRESSURE;
    }

    e->pending_bytes = zb_ring_len(&e->ring);
    e->frames_in++;
    e->bytes_in += (uint64_t)wrote;
    return ZB_STREAM_OK;
}

size_t zb_stream_pop(zb_stream_table_t *tbl, uint32_t stream_id,
                     uint8_t *dst, size_t len) {
    zb_stream_entry_t *e = find_entry(tbl, stream_id);
    if (!e || !dst || len == 0) {
        return 0;
    }
    size_t n = zb_ring_read(&e->ring, dst, len);
    e->pending_bytes = zb_ring_len(&e->ring);
    return n;
}

const zb_stream_entry_t *zb_stream_get(const zb_stream_table_t *tbl, uint32_t stream_id) {
    if (!tbl) {
        return NULL;
    }
    for (size_t i = 0; i < ZB_MAX_STREAMS; i++) {
        if (tbl->entries[i].active && tbl->entries[i].stream_id == stream_id) {
            return &tbl->entries[i];
        }
    }
    return NULL;
}
