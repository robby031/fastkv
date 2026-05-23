#define _POSIX_C_SOURCE 200809L

#include "bench_main.h"
#include "fastkv.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bench_sequential_write(fastkv_db_t *db, uint64_t n, bench_hist_t *h) {
    char key[24];
    for (uint64_t i = 0; i < n; i++) {
        /* wrap ke key space warmup supaya hanya overwrite, tidak terus menambah versi baru */
        int      klen = snprintf(key, sizeof key, "%020" PRIu64, i % BENCH_KEY_SPACE);
        uint64_t t0   = bench_now_ns();
        fastkv_put(db, FASTKV_SLICE(key, (size_t)klen),
                   FASTKV_STR("value_32bytes_padding_data_____"));
        bench_hist_record(h, bench_now_ns() - t0);
    }
}

void bench_mixed_rw(fastkv_db_t *db, uint64_t n, bench_hist_t *h) {
    char key[24];
    for (uint64_t i = 0; i < n; i++) {
        uint64_t t0;
        if (i % 2 == 0) {
            int klen = snprintf(key, sizeof key, "%020" PRIu64, i % BENCH_KEY_SPACE);
            t0 = bench_now_ns();
            fastkv_put(db, FASTKV_SLICE(key, (size_t)klen),
                       FASTKV_STR("value_32bytes_padding_data_____"));
        } else {
            int klen = snprintf(key, sizeof key, "%020" PRIu64, i % BENCH_KEY_SPACE);
            fastkv_slice_t v = FASTKV_SLICE_NULL;
            t0 = bench_now_ns();
            fastkv_get(db, FASTKV_SLICE(key, (size_t)klen), &v);
        }
        bench_hist_record(h, bench_now_ns() - t0);
    }
}

void bench_txn_batch(fastkv_db_t *db, uint64_t n, bench_hist_t *h) {
    char key[24];
    for (uint64_t i = 0; i < n; i++) {
        fastkv_txn_t *txn = NULL;
        fastkv_txn_begin(db, false, &txn);
        uint64_t t0 = bench_now_ns();
        for (int j = 0; j < 10; j++) {
            int klen = snprintf(key, sizeof key, "%020" PRIu64,
                                (i * 10 + (uint64_t)j) % BENCH_KEY_SPACE);
            fastkv_txn_put(txn, FASTKV_SLICE(key, (size_t)klen), FASTKV_STR("txn_val"));
        }
        fastkv_txn_commit(txn);
        bench_hist_record(h, bench_now_ns() - t0);
    }
}

/* concurrent mixed benchmark */

typedef struct {
    fastkv_db_t      *db;
    uint64_t          n;
    int               thread_id;
    _Atomic uint64_t *counter;
} conc_arg_t;

static void *conc_worker(void *arg) {
    conc_arg_t *a   = arg;
    char        key[24];
    uint64_t    rng = 0xdeadbeef12345678ULL ^ (uint64_t)a->thread_id;

    for (uint64_t i = 0; i < a->n; i++) {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;

        int klen = snprintf(key, sizeof key, "%020" PRIu64, rng % BENCH_KEY_SPACE);
        if (i % 2 == 0) {
            fastkv_put(a->db, FASTKV_SLICE(key, (size_t)klen),
                       FASTKV_STR("concurrent_value"));
        } else {
            fastkv_slice_t v = FASTKV_SLICE_NULL;
            fastkv_get(a->db, FASTKV_SLICE(key, (size_t)klen), &v);
        }
        atomic_fetch_add_explicit(a->counter, 1, memory_order_relaxed);
    }
    return NULL;
}

uint64_t bench_concurrent_mixed(fastkv_db_t *db, uint64_t n, int threads) {
    _Atomic uint64_t counter = 0;
    pthread_t  *tids = malloc((size_t)threads * sizeof(pthread_t));
    conc_arg_t *args = malloc((size_t)threads * sizeof(conc_arg_t));
    uint64_t    per  = n / (uint64_t)threads;

    for (int i = 0; i < threads; i++) {
        args[i] = (conc_arg_t){ .db = db, .n = per, .thread_id = i, .counter = &counter };
        pthread_create(&tids[i], NULL, conc_worker, &args[i]);
    }
    for (int i = 0; i < threads; i++)
        pthread_join(tids[i], NULL);

    uint64_t result = atomic_load(&counter);
    free(tids);
    free(args);
    return result;
}
