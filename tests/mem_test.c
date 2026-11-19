#include "zb_mem.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint8_t arena_mem[256];
    zb_arena_t a;
    assert(zb_arena_init(&a, arena_mem, sizeof(arena_mem)) == ZB_MEM_OK);

    uint8_t *p1 = (uint8_t *)zb_arena_alloc(&a, 5, 8);
    assert(p1 != NULL);
    assert(((uintptr_t)p1 & 7u) == 0);
    memcpy(p1, "ABCDE", 5);

    uint32_t *p2 = (uint32_t *)zb_arena_alloc(&a, sizeof(uint32_t), 8);
    assert(p2 != NULL);
    assert(((uintptr_t)p2 & 7u) == 0);
    *p2 = 0x12345678u;

    assert(zb_arena_alloc(&a, 1024, 8) == NULL);
    zb_arena_reset(&a);
    assert(zb_arena_used(&a) == 0);

    uint8_t ring_mem[16];
    zb_ring_t r;
    assert(zb_ring_init(&r, ring_mem, sizeof(ring_mem)) == ZB_MEM_OK);

    const uint8_t w1[] = "0123456789AB";
    assert(zb_ring_write(&r, w1, sizeof(w1) - 1) == 12);

    uint8_t out[8];
    assert(zb_ring_peek(&r, out, 5) == 5);
    assert(memcmp(out, "01234", 5) == 0);
    assert(zb_ring_read(&r, out, 7) == 7);
    assert(memcmp(out, "0123456", 7) == 0);
    assert(zb_ring_len(&r) == 5);

    const uint8_t w2[] = "CDEFGHIJKLMN";
    assert(zb_ring_write(&r, w2, sizeof(w2) - 1) == 11);
    assert(zb_ring_len(&r) == 16);
    assert(zb_ring_avail(&r) == 0);

    uint8_t full_out[16];
    assert(zb_ring_read(&r, full_out, 16) == 16);
    assert(memcmp(full_out, "789ABCDEFGHIJKLM", 16) == 0);
    assert(zb_ring_len(&r) == 0);

    printf("mem_test passed! arena_peak=%zu\n", a.peak);
    return 0;
}
