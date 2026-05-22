#include "bench_main.h"

#include "fastkv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static fastkv_db_t *g_db;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void bench_setup(void) {
    system("rm -rf /tmp/fastkv_bench && mkdir -p /tmp/fastkv_bench");
    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = "/tmp/fastkv_bench";
    opts.sync_writes   = false;
    opts.map_size      = 4 * 1024 * 1024;
    fastkv_open(&g_db, &opts);
}

void bench_teardown(void) {
    fastkv_close(g_db);
    system("rm -rf /tmp/fastkv_bench");
}

fastkv_db_t *bench_db(void) {
    return g_db;
}

void bench_report(const char *name, uint64_t ops, uint64_t elapsed_ns) {
    double ops_per_sec = (double)ops / ((double)elapsed_ns / 1e9);
    double ns_per_op   = (double)elapsed_ns / (double)ops;
    printf("%-30s  %10.0f ops/s  %8.1f ns/op\n", name, ops_per_sec, ns_per_op);
}

void bench_run(const char *name, bench_fn fn, uint64_t ops) {
    uint64_t t0 = now_ns();
    fn(g_db, ops);
    uint64_t elapsed = now_ns() - t0;
    bench_report(name, ops, elapsed);
}

extern void bench_sequential_write(fastkv_db_t *db, uint64_t n);
extern void bench_sequential_read(fastkv_db_t *db, uint64_t n);
extern void bench_random_read(fastkv_db_t *db, uint64_t n);
extern void bench_mixed_rw(fastkv_db_t *db, uint64_t n);

int main(void) {
    bench_setup();
    printf("%s\n", "-----------------------------------------------------------------");
    printf("%-30s  %10s  %12s\n", "Benchmark", "ops/s", "ns/op");
    printf("%s\n", "-----------------------------------------------------------------\n");

    bench_run("sequential_write", bench_sequential_write, 500000);
    bench_run("sequential_read", bench_sequential_read, 500000);
    bench_run("random_read", bench_random_read, 500000);
    bench_run("mixed_rw_50_50", bench_mixed_rw, 500000);
    printf("%s\n", "-----------------------------------------------------------------\n");

    bench_teardown();
    return 0;
}
