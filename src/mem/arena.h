#ifndef FASTKV_MEM_ARENA_H
#define FASTKV_MEM_ARENA_H

#include <stddef.h>
#include <stdint.h>

/*
 * Region-based (arena) allocator for short-lived objects.
 *
 * All allocations bump a pointer forward; the entire arena is
 * released in O(1) at reset/destroy time. Used for per-transaction
 * scratch space (MVCC version chains, write-set entries, etc.).
 */

#define FASTKV_ARENA_DEFAULT_BLOCK (64u * 1024u)  /* 64 KiB */

typedef struct fastkv_arena_block {
    struct fastkv_arena_block *next;
    size_t   cap;
    size_t   used;
    uint8_t  data[];
} fastkv_arena_block_t;

typedef struct {
    fastkv_arena_block_t *head;    /* current (newest) block */
    fastkv_arena_block_t *base;    /* first block (reused on reset) */
    size_t                block_size;
    size_t                total_allocated;
} fastkv_arena_t;

fastkv_arena_t *fastkv_arena_create(size_t block_size);
void            fastkv_arena_destroy(fastkv_arena_t *arena);
void            fastkv_arena_reset(fastkv_arena_t *arena);   /* keep memory, zero cursors */

void *fastkv_arena_alloc(fastkv_arena_t *arena, size_t size);
void *fastkv_arena_alloc_aligned(fastkv_arena_t *arena, size_t size, size_t align);

/* Duplicate a byte buffer into the arena */
void *fastkv_arena_dup(fastkv_arena_t *arena, const void *buf, size_t len);

#endif /* FASTKV_MEM_ARENA_H */
