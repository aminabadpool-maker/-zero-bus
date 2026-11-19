#include "zb_mem.h"
#include <string.h>

static int is_pow2_size(size_t x) {
    return x > 0 && (x & (x - 1u)) == 0;
}

zb_mem_status_t zb_arena_init(zb_arena_t *a, uint8_t *backing, size_t cap) {
    if (!a || !backing || cap == 0) {
        return ZB_MEM_ERR_ARG;
    }
    a->base = backing;
    a->cap = cap;
    a->offset = 0;
    a->peak = 0;
    return ZB_MEM_OK;
}

void *zb_arena_alloc(zb_arena_t *a, size_t size, size_t align) {
    if (!a || !a->base || size == 0) {
        return NULL;
    }
    if (align == 0) {
        align = 8u;
    }
    if (!is_pow2_size(align)) {
        return NULL;
    }

    uintptr_t cur = (uintptr_t)(a->base + a->offset);
    uintptr_t mask = (uintptr_t)(align - 1u);
    if (cur > UINTPTR_MAX - mask) {
        return NULL;
    }
    uintptr_t aligned = (cur + mask) & ~mask;
    size_t pad = (size_t)(aligned - cur);

    if (pad > a->cap - a->offset) {
        return NULL;
    }
    size_t start_off = a->offset + pad;
    if (size > a->cap - start_off) {
        return NULL;
    }

    a->offset = start_off + size;
    if (a->offset > a->peak) {
        a->peak = a->offset;
    }
    return a->base + start_off;
}

void zb_arena_reset(zb_arena_t *a) {
    if (!a) {
        return;
    }
    a->offset = 0;
}

size_t zb_arena_used(const zb_arena_t *a) {
    if (!a) {
        return 0;
    }
    return a->offset;
}

zb_mem_status_t zb_ring_init(zb_ring_t *r, uint8_t *backing, size_t cap) {
    if (!r || !backing || cap == 0) {
        return ZB_MEM_ERR_ARG;
    }
    r->buf = backing;
    r->cap = cap;
    r->head = 0;
    r->tail = 0;
    r->len = 0;
    return ZB_MEM_OK;
}

void zb_ring_reset(zb_ring_t *r) {
    if (!r) {
        return;
    }
    r->head = 0;
    r->tail = 0;
    r->len = 0;
}

size_t zb_ring_len(const zb_ring_t *r) {
    return r ? r->len : 0;
}

size_t zb_ring_avail(const zb_ring_t *r) {
    if (!r || r->len > r->cap) {
        return 0;
    }
    return r->cap - r->len;
}

size_t zb_ring_write(zb_ring_t *r, const uint8_t *src, size_t len) {
    if (!r || !r->buf || !src || len == 0) {
        return 0;
    }
    size_t avail = zb_ring_avail(r);
    size_t to_write = (len < avail) ? len : avail;
    size_t written = 0;

    while (written < to_write) {
        size_t chunk = r->cap - r->tail;
        size_t rem = to_write - written;
        if (chunk > rem) {
            chunk = rem;
        }
        memcpy(r->buf + r->tail, src + written, chunk);
        r->tail = (r->tail + chunk) % r->cap;
        r->len += chunk;
        written += chunk;
    }
    return written;
}

static size_t ring_copy_out(const zb_ring_t *r, uint8_t *dst, size_t len) {
    if (!r || !r->buf || !dst || len == 0 || r->len == 0) {
        return 0;
    }
    size_t to_copy = (len < r->len) ? len : r->len;
    size_t pos = r->head;
    size_t copied = 0;

    while (copied < to_copy) {
        size_t chunk = r->cap - pos;
        size_t rem = to_copy - copied;
        if (chunk > rem) {
            chunk = rem;
        }
        memcpy(dst + copied, r->buf + pos, chunk);
        pos = (pos + chunk) % r->cap;
        copied += chunk;
    }
    return copied;
}

size_t zb_ring_peek(const zb_ring_t *r, uint8_t *dst, size_t len) {
    return ring_copy_out(r, dst, len);
}

size_t zb_ring_discard(zb_ring_t *r, size_t len) {
    if (!r || len == 0 || r->len == 0) {
        return 0;
    }
    size_t to_drop = (len < r->len) ? len : r->len;
    r->head = (r->head + to_drop) % r->cap;
    r->len -= to_drop;
    return to_drop;
}

size_t zb_ring_read(zb_ring_t *r, uint8_t *dst, size_t len) {
    size_t n = ring_copy_out(r, dst, len);
    if (n > 0) {
        zb_ring_discard(r, n);
    }
    return n;
}
