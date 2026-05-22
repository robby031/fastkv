#include "mvcc.h"

void fastkv_oracle_init(fastkv_oracle_t *oracle)
{
 atomic_init(&oracle->clock,  1); /* 0 is reserved for "invalid" */
 atomic_init(&oracle->min_active, 1);
}

fastkv_ts_t fastkv_oracle_begin(fastkv_oracle_t *oracle)
{
 return atomic_fetch_add_explicit(&oracle->clock, 1, memory_order_acq_rel);
}

fastkv_ts_t fastkv_oracle_commit(fastkv_oracle_t *oracle)
{
 return atomic_fetch_add_explicit(&oracle->clock, 1, memory_order_acq_rel);
}

fastkv_ts_t fastkv_oracle_now(fastkv_oracle_t *oracle)
{
 return atomic_load_explicit(&oracle->clock, memory_order_acquire);
}

void fastkv_oracle_release(fastkv_oracle_t *oracle, fastkv_ts_t begin_ts)
{
 /*
 * Simple eager update: advance min_active if this was the smallest.
 * A production implementation would track all active begin_ts values
 * in a min-heap or epoch-based structure.
 */
 fastkv_ts_t expected = begin_ts;
 atomic_compare_exchange_strong_explicit(
 &oracle->min_active, &expected,
 atomic_load_explicit(&oracle->clock, memory_order_relaxed),
 memory_order_acq_rel, memory_order_relaxed);
}
