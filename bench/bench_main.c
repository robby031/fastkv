#define _POSIX_C_SOURCE 200809L

#include "bench_main.h"

#include "fastkv.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BENCH_OPS 5000000
#define BENCH_OPS_CONC 10000000
#define WARMUP_KEYS BENCH_KEY_SPACE /* warmup = key space yang dipakai semua bench */
#define CONC_THREADS 4

static fastkv_db_t *g_db;

uint64_t bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

fastkv_db_t *bench_db(void) {
    return g_db;
}

bench_hist_t *bench_hist_new(size_t cap) {
    bench_hist_t *h = malloc(sizeof(*h));
    h->samples      = malloc(cap * sizeof(uint64_t));
    h->count        = 0;
    h->cap          = cap;
    return h;
}

void bench_hist_record(bench_hist_t *h, uint64_t ns) {
    if (h && h->count < h->cap)
        h->samples[h->count++] = ns;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

uint64_t bench_hist_percentile(bench_hist_t *h, double pct) {
    if (!h || h->count == 0)
        return 0;
    qsort(h->samples, h->count, sizeof(uint64_t), cmp_u64);
    size_t idx = (size_t)(pct / 100.0 * (double)(h->count - 1));
    return h->samples[idx];
}

void bench_hist_free(bench_hist_t *h) {
    if (!h)
        return;
    free(h->samples);
    free(h);
}

void bench_print_header(void) {
    printf("\n");
    printf("%-34s  %12s  %10s  %10s  %10s  %10s\n", "Benchmark", "ops/s", "p50 us", "p99 us",
        "p99.9 us", "ns/op");
    printf("%-34s  %12s  %10s  %10s  %10s  %10s\n", "----------------------------------",
        "------------", "----------", "----------", "----------", "----------");
}

void bench_print_row(const char *name, uint64_t ops, uint64_t elapsed_ns, bench_hist_t *hist) {
    double ops_per_sec = (double)ops / ((double)elapsed_ns / 1e9);
    double ns_per_op   = (double)elapsed_ns / (double)ops;

    if (hist && hist->count > 0) {
        double p50  = (double)bench_hist_percentile(hist, 50.0) / 1000.0;
        double p99  = (double)bench_hist_percentile(hist, 99.0) / 1000.0;
        double p999 = (double)bench_hist_percentile(hist, 99.9) / 1000.0;
        printf("%-34s  %12.0f  %10.2f  %10.2f  %10.2f  %10.1f\n", name, ops_per_sec, p50, p99, p999,
            ns_per_op);
    } else {
        printf("%-34s  %12.0f  %10s  %10s  %10s  %10.1f\n", name, ops_per_sec, "-", "-", "-",
            ns_per_op);
    }
}

void bench_print_footer(void) {
    printf("%-34s  %12s  %10s  %10s  %10s  %10s\n", "----------------------------------",
        "------------", "----------", "----------", "----------", "----------");
    printf("\n");
}

void bench_kpi_check(
    const char *name, double actual, double target, const char *unit, int higher_is_better) {
    int pass = higher_is_better ? (actual >= target) : (actual <= target);
    printf("  %-44s  actual %9.1f %-6s  target %s %.0f  [%s]\n", name, actual, unit,
        higher_is_better ? ">=" : "<=", target, pass ? "PASS" : "FAIL");
}

static void warmup(void) {
    char key[24];
    for (uint64_t i = 0; i < WARMUP_KEYS; i++) {
        int klen = snprintf(key, sizeof key, "%020" PRIu64, i);
        fastkv_put(
            g_db, FASTKV_SLICE(key, (size_t)klen), FASTKV_STR("value_32bytes_padding_data_____"));
    }
}

extern void     bench_sequential_write(fastkv_db_t *db, uint64_t n, bench_hist_t *h);
extern void     bench_sequential_read(fastkv_db_t *db, uint64_t n, bench_hist_t *h);
extern void     bench_random_read(fastkv_db_t *db, uint64_t n, bench_hist_t *h);
extern void     bench_mixed_rw(fastkv_db_t *db, uint64_t n, bench_hist_t *h);
extern void     bench_txn_batch(fastkv_db_t *db, uint64_t n, bench_hist_t *h);
extern uint64_t bench_concurrent_mixed(fastkv_db_t *db, uint64_t n, int threads);

int main(void) {
    fastkv_set_log_level(FASTKV_LOG_SILENT);

    system("rm -rf /tmp/fastkv_bench && mkdir -p /tmp/fastkv_bench");
    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = "/tmp/fastkv_bench";
    opts.sync_writes   = false;
    /* map_size = kapasitas bucket hash table.
     * Key space terbatas ke BENCH_KEY_SPACE, jadi cukup 4x itu untuk load factor 25%. */
    opts.map_size = (size_t)BENCH_KEY_SPACE * 4;
    fastkv_open(&g_db, &opts);
    warmup();

    printf("FastKV Benchmark Suite\n");
    printf("ops/bench: %d   warmup_keys: %d   threads (concurrent): %d\n", BENCH_OPS, WARMUP_KEYS,
        CONC_THREADS);

    bench_print_header();

    bench_hist_t *h;
    uint64_t      t0, elapsed;

    h  = bench_hist_new(BENCH_OPS);
    t0 = bench_now_ns();
    bench_sequential_write(g_db, BENCH_OPS, h);
    elapsed = bench_now_ns() - t0;
    bench_print_row("sequential_write", BENCH_OPS, elapsed, h);
    bench_hist_free(h);

    h  = bench_hist_new(BENCH_OPS);
    t0 = bench_now_ns();
    bench_sequential_read(g_db, BENCH_OPS, h);
    elapsed          = bench_now_ns() - t0;
    double read_p999 = (double)bench_hist_percentile(h, 99.9) / 1000.0;
    bench_print_row("sequential_read", BENCH_OPS, elapsed, h);
    bench_hist_free(h);

    h  = bench_hist_new(BENCH_OPS);
    t0 = bench_now_ns();
    bench_random_read(g_db, BENCH_OPS, h);
    elapsed = bench_now_ns() - t0;
    bench_print_row("random_read", BENCH_OPS, elapsed, h);
    bench_hist_free(h);

    h  = bench_hist_new(BENCH_OPS);
    t0 = bench_now_ns();
    bench_mixed_rw(g_db, BENCH_OPS, h);
    elapsed           = bench_now_ns() - t0;
    double write_p999 = (double)bench_hist_percentile(h, 99.9) / 1000.0;
    bench_print_row("mixed_rw_50_50 (1 thread)", BENCH_OPS, elapsed, h);
    bench_hist_free(h);

    h  = bench_hist_new(BENCH_OPS / 10);
    t0 = bench_now_ns();
    bench_txn_batch(g_db, BENCH_OPS / 10, h);
    elapsed = bench_now_ns() - t0;
    bench_print_row("txn_batch (10 ops/commit)", BENCH_OPS / 10, elapsed, h);
    bench_hist_free(h);

    /* GC version nodes lama, lalu pause compaction thread selama concurrent bench
     * supaya background GC tidak mem-free nodes yang sedang dibaca reader thread */
    fastkv_sync(g_db);
    fastkv_compaction_pause(g_db);

    t0             = bench_now_ns();
    uint64_t c_ops = bench_concurrent_mixed(g_db, BENCH_OPS_CONC, CONC_THREADS);
    elapsed        = bench_now_ns() - t0;
    double c_ops_s = (double)c_ops / ((double)elapsed / 1e9);
    bench_print_row("mixed_rw_50_50 (16 thread)", c_ops, elapsed, NULL);

    bench_print_footer();

    printf("KPI Validation (Roadmap v1.0)\n");
    bench_kpi_check("read P99.9 latency", read_p999, 50.0, "us", 0);
    bench_kpi_check("write P99.9 latency", write_p999, 100.0, "us", 0);
    bench_kpi_check("mixed 50/50 throughput (16c)", c_ops_s, 500000.0, "ops/s", 1);
    printf("\n");

    fastkv_compaction_resume(g_db);
    fastkv_close(g_db);
    system("rm -rf /tmp/fastkv_bench");
    return 0;
}
