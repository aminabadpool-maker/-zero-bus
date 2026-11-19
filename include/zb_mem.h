#ifndef ZB_MEM_H
#define ZB_MEM_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ZB_MEM_OK = 0,
    ZB_MEM_ERR_ARG = -1,
    ZB_MEM_ERR_ALIGN = -2,
    ZB_MEM_ERR_FULL = -3,
    ZB_MEM_ERR_EMPTY = -4,
    ZB_MEM_ERR_SIZE = -5
} zb_mem_status_t;

typedef struct {
    uint8_t *base;
    size_t cap;
    size_t offset;
    size_t peak;
} zb_arena_t;

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t head;
    size_t tail;
    size_t len;
} zb_ring_t;

zb_mem_status_t zb_arena_init(zb_arena_t *a, uint8_t *backing, size_t cap);
void *zb_arena_alloc(zb_arena_t *a, size_t size, size_t align);
void zb_arena_reset(zb_arena_t *a);
size_t zb_arena_used(const zb_arena_t *a);

zb_mem_status_t zb_ring_init(zb_ring_t *r, uint8_t *backing, size_t cap);
void zb_ring_reset(zb_ring_t *r);
size_t zb_ring_len(const zb_ring_t *r);
size_t zb_ring_avail(const zb_ring_t *r);
size_t zb_ring_write(zb_ring_t *r, const uint8_t *src, size_t len);
size_t zb_ring_peek(const zb_ring_t *r, uint8_t *dst, size_t len);
size_t zb_ring_read(zb_ring_t *r, uint8_t *dst, size_t len);
size_t zb_ring_discard(zb_ring_t *r, size_t len);

#endif
