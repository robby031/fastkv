#include "fastkv.h"
#include <stdio.h>
#include <string.h>

void bench_sequential_write(fastkv_db_t *db, uint64_t n)
{
 char key[32];
 for (uint64_t i = 0; i < n; i++) {
 int klen = snprintf(key, sizeof key, "key_%020llu", (unsigned long long)i);
 fastkv_put(db,
 FASTKV_SLICE(key, (size_t)klen),
 FASTKV_STR("value_placeholder_data"));
 }
}

void bench_mixed_rw(fastkv_db_t *db, uint64_t n)
{
 char key[32];
 for (uint64_t i = 0; i < n; i++) {
 int klen = snprintf(key, sizeof key, "key_%020llu", (unsigned long long)(i % 100000));
 fastkv_slice_t k = FASTKV_SLICE(key, (size_t)klen);

 if (i % 2 == 0) {
 fastkv_put(db, k, FASTKV_STR("val"));
 } else {
 fastkv_slice_t v;
 fastkv_get(db, k, &v);
 }
 }
}
