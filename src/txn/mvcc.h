#ifndef FASTKV_TXN_MVCC_H
#define FASTKV_TXN_MVCC_H

#include "fastkv/types.h"
#include <stdatomic.h>

/*
 * Timestamp Oracle — monotonic atomic counter.
 *
 * begin_ts = fetch_add(1)  when a transaction starts
 * commit_ts  = fetch_add(1)  at commit time (must be > begin_ts of any
 * concurrent writer to detect WW conflicts)
 */

typedef struct {
 _Atomic fastkv_ts_t clock; /* global logical clock */
 _Atomic fastkv_ts_t min_active; /* smallest begin_ts of any live txn */
} fastkv_oracle_t;

void  fastkv_oracle_init(fastkv_oracle_t *oracle);
fastkv_ts_t fastkv_oracle_begin(fastkv_oracle_t *oracle);
fastkv_ts_t fastkv_oracle_commit(fastkv_oracle_t *oracle);
fastkv_ts_t fastkv_oracle_now(fastkv_oracle_t *oracle);

/* Called by txn manager when a txn finishes, to advance min_active */
void fastkv_oracle_release(fastkv_oracle_t *oracle, fastkv_ts_t begin_ts);

#endif /* FASTKV_TXN_MVCC_H */
