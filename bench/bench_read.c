#define _POSIX_C_SOURCE 200809L

#include "bench_main.h"
#include "fastkv.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

void bench_sequential_read(fastkv_db_t *db, uint64_t n, bench_hist_t *h) {
    char key[24];
    for (uint64_t i = 0; i < n; i++) {
        int klen = snprintf(key, sizeof key, "%020" PRIu64, i % BENCH_KEY_SPACE);
        fastkv_slice_t val = FASTKV_SLICE_NULL;
        uint64_t t0 = bench_now_ns();
        fastkv_get(db, FASTKV_SLICE(key, (size_t)klen), &val);
        bench_hist_record(h, bench_now_ns() - t0);
    }
}

void bench_random_read(fastkv_db_t *db, uint64_t n, bench_hist_t *h) {
    char key[24];
    uint64_t rng = 0xdeadbeefcafebabe;
    for (uint64_t i = 0; i < n; i++) {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        int klen = snprintf(key, sizeof key, "%020" PRIu64, rng % BENCH_KEY_SPACE);
        fastkv_slice_t val = FASTKV_SLICE_NULL;
        uint64_t t0 = bench_now_ns();
        fastkv_get(db, FASTKV_SLICE(key, (size_t)klen), &val);
        bench_hist_record(h, bench_now_ns() - t0);
    }
}
