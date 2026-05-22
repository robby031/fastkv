#ifndef FASTKV_UTIL_HASH_H
#define FASTKV_UTIL_HASH_H

#include <stddef.h>
#include <stdint.h>

/* 64-bit wyhash — extremely fast, good avalanche */
uint64_t fastkv_hash64(const void *key, size_t len, uint64_t seed);

/* Convenience zero-seed variant */
static inline uint64_t fastkv_hash(const void *key, size_t len) {
    return fastkv_hash64(key, len, 0);
}

#endif /* FASTKV_UTIL_HASH_H */
