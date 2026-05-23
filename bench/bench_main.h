#ifndef FASTKV_BENCH_MAIN_H
#define FASTKV_BENCH_MAIN_H

#include "fastkv.h"

#include <stddef.h>
#include <stdint.h>

/* key space yang di-warmup; semua benchmark write wrap di sini
 * supaya tidak terus menambah version nodes baru tak terbatas */
#ifndef BENCH_KEY_SPACE
#define BENCH_KEY_SPACE 500000ULL
#endif

/* timer */
uint64_t bench_now_ns(void);

/* DB shared */
fastkv_db_t *bench_db(void);

/* histogram untuk latency percentile */
typedef struct {
    uint64_t *samples;
    size_t    count;
    size_t    cap;
} bench_hist_t;

bench_hist_t *bench_hist_new(size_t cap);
void          bench_hist_record(bench_hist_t *h, uint64_t ns);
uint64_t      bench_hist_percentile(bench_hist_t *h, double pct);
void          bench_hist_free(bench_hist_t *h);

/* laporan */
void bench_print_header(void);
void bench_print_row(const char *name, uint64_t ops, uint64_t elapsed_ns, bench_hist_t *hist);
void bench_print_footer(void);

/* KPI check */
void bench_kpi_check(
    const char *name, double actual, double target, const char *unit, int higher_is_better);

#endif
