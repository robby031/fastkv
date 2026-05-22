#ifndef FASTKV_TXN_MVCC_H
#define FASTKV_TXN_MVCC_H

#include "fastkv/types.h"

#include <stdatomic.h>
#include <stdbool.h>

/*
 * Timestamp Oracle
 *
 * Slot-based active-transaction tracker.  Each txn claims one slot on begin;
 * the slot stores its begin_ts.  On commit/abort the slot is zeroed.
 *
 * min_active_ts = min of all non-zero slots (or current clock when idle).
 * This value drives the MVCC garbage collector: versions whose end_ts is
 * less than min_active_ts are invisible to all living transactions and can
 * be freed.
 */

#define FASTKV_MAX_CONCURRENT_TXN 256

typedef struct {
    _Atomic fastkv_ts_t clock;                            /* monotonic logical clock  */
    _Atomic fastkv_ts_t slots[FASTKV_MAX_CONCURRENT_TXN]; /* 0 = slot free            */
} fastkv_oracle_t;

void fastkv_oracle_init(fastkv_oracle_t *oracle);

/* Returns begin_ts and claims a slot; slot_out receives the slot index. */
fastkv_ts_t fastkv_oracle_begin(fastkv_oracle_t *oracle, int *slot_out);

fastkv_ts_t fastkv_oracle_commit(fastkv_oracle_t *oracle);
fastkv_ts_t fastkv_oracle_now(fastkv_oracle_t *oracle);

/* Release a previously claimed slot (called on commit/abort). */
void fastkv_oracle_release(fastkv_oracle_t *oracle, int slot);

/* Compute min begin_ts of all active transactions (safe for GC). */
fastkv_ts_t fastkv_oracle_min_active(fastkv_oracle_t *oracle);

/* Advance the internal clock to at_least if it is currently behind.
 * Used after loading a snapshot so the clock does not go backwards. */
void fastkv_oracle_advance(fastkv_oracle_t *oracle, fastkv_ts_t at_least);

#endif /* FASTKV_TXN_MVCC_H */
