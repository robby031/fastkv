#include "allocator.h"
#include <stdlib.h>

fastkv_allocator_t g_fastkv_allocator = {
 .malloc_fn  = malloc,
 .free_fn = free,
 .realloc_fn = realloc,
};

void fastkv_allocator_init(void *(*malloc_fn)(size_t), void  (*free_fn)(void *), void *(*realloc_fn)(void *, size_t))
{
 if (malloc_fn)  g_fastkv_allocator.malloc_fn  = malloc_fn;
 if (free_fn) g_fastkv_allocator.free_fn = free_fn;
 if (realloc_fn) g_fastkv_allocator.realloc_fn = realloc_fn;
}
