#ifndef FASTKV_MEM_ALLOCATOR_H
#define FASTKV_MEM_ALLOCATOR_H

#include <stddef.h>

/* Pluggable allocator vtable — defaults to system malloc/free */
typedef struct {
    void *(*malloc_fn)(size_t size);
    void  (*free_fn)(void *ptr);
    void *(*realloc_fn)(void *ptr, size_t size);
} fastkv_allocator_t;

extern fastkv_allocator_t g_fastkv_allocator;

void fastkv_allocator_init(void *(*malloc_fn)(size_t),
                            void  (*free_fn)(void *),
                            void *(*realloc_fn)(void *, size_t));

static inline void *fkv_malloc(size_t n)          { return g_fastkv_allocator.malloc_fn(n);    }
static inline void  fkv_free(void *p)              {        g_fastkv_allocator.free_fn(p);      }
static inline void *fkv_realloc(void *p, size_t n) { return g_fastkv_allocator.realloc_fn(p,n); }

#endif /* FASTKV_MEM_ALLOCATOR_H */
