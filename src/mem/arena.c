#include "arena.h"

#include "allocator.h"

#include <assert.h>
#include <string.h>

static fastkv_arena_block_t *block_new(size_t cap) {
    fastkv_arena_block_t *b = fkv_malloc(sizeof(*b) + cap);
    if (!b)
        return NULL;
    b->next = NULL;
    b->cap  = cap;
    b->used = 0;
    return b;
}

fastkv_arena_t *fastkv_arena_create(size_t block_size) {
    if (block_size == 0)
        block_size = FASTKV_ARENA_DEFAULT_BLOCK;

    fastkv_arena_t *a = fkv_malloc(sizeof(*a));
    if (!a)
        return NULL;

    a->block_size      = block_size;
    a->total_allocated = 0;
    a->base            = block_new(block_size);
    a->head            = a->base;

    if (!a->base) {
        fkv_free(a);
        return NULL;
    }
    return a;
}

void fastkv_arena_reset(fastkv_arena_t *arena) {
    /* Free all blocks except the first one */
    fastkv_arena_block_t *b = arena->base->next;
    while (b) {
        fastkv_arena_block_t *next = b->next;
        fkv_free(b);
        b = next;
    }
    arena->base->next      = NULL;
    arena->base->used      = 0;
    arena->head            = arena->base;
    arena->total_allocated = 0;
}

void fastkv_arena_destroy(fastkv_arena_t *arena) {
    fastkv_arena_block_t *b = arena->base;
    while (b) {
        fastkv_arena_block_t *next = b->next;
        fkv_free(b);
        b = next;
    }
    fkv_free(arena);
}

void *fastkv_arena_alloc_aligned(fastkv_arena_t *arena, size_t size, size_t align) {
    assert((align & (align - 1)) == 0); /* align must be power of two */

    fastkv_arena_block_t *b = arena->head;
    /* Align based on absolute address, not just the relative offset */
    uintptr_t base    = (uintptr_t)(b->data + b->used);
    uintptr_t aligned = (base + (uintptr_t)(align - 1)) & ~(uintptr_t)(align - 1);
    size_t    offset  = (size_t)(aligned - (uintptr_t)b->data);

    if (offset + size > b->cap) {
        size_t new_cap = arena->block_size;
        if (size > new_cap)
            new_cap = size + align;
        fastkv_arena_block_t *nb = block_new(new_cap);
        if (!nb)
            return NULL;
        b->next     = nb;
        arena->head = nb;
        b           = nb;
        offset      = 0;
    }

    void *ptr = b->data + offset;
    b->used   = offset + size;
    arena->total_allocated += size;
    return ptr;
}

void *fastkv_arena_alloc(fastkv_arena_t *arena, size_t size) {
    return fastkv_arena_alloc_aligned(arena, size, _Alignof(max_align_t));
}

void *fastkv_arena_dup(fastkv_arena_t *arena, const void *buf, size_t len) {
    void *dst = fastkv_arena_alloc(arena, len);
    if (dst)
        memcpy(dst, buf, len);
    return dst;
}
