#include "fastkv.h"
#include <stdio.h>

void bench_sequential_read(fastkv_db_t *db, uint64_t n)
{
    char key[32];
    for (uint64_t i = 0; i < n; i++) {
        int klen = snprintf(key, sizeof key, "key_%020llu", (unsigned long long)(i % n));
        fastkv_slice_t val;
        fastkv_get(db, FASTKV_SLICE(key, (size_t)klen), &val);
    }
}

void bench_random_read(fastkv_db_t *db, uint64_t n)
{
    char key[32];
    uint64_t rng = 0xdeadbeefcafebabe;
    for (uint64_t i = 0; i < n; i++) {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;  /* xorshift64 */
        int klen = snprintf(key, sizeof key, "key_%020llu", (unsigned long long)(rng % n));
        fastkv_slice_t val;
        fastkv_get(db, FASTKV_SLICE(key, (size_t)klen), &val);
    }
}
