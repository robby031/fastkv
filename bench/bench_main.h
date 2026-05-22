#ifndef FASTKV_BENCH_MAIN_H
#define FASTKV_BENCH_MAIN_H

#include "fastkv.h"

#include <stdint.h>

typedef void (*bench_fn)(fastkv_db_t *db, uint64_t n);

fastkv_db_t *bench_db(void);
void         bench_report(const char *name, uint64_t ops, uint64_t elapsed_ns);

#endif /* FASTKV_BENCH_MAIN_H */
