#include "mvcc.h"

void fastkv_oracle_init(fastkv_oracle_t *oracle) {
    atomic_init(&oracle->clock, 1); /* 0 is "invalid", start from 1 */
    for (int i = 0; i < FASTKV_MAX_CONCURRENT_TXN; i++)
        atomic_init(&oracle->slots[i], 0);
}

fastkv_ts_t fastkv_oracle_begin(fastkv_oracle_t *oracle, int *slot_out) {
    fastkv_ts_t ts = atomic_fetch_add_explicit(&oracle->clock, 1, memory_order_acq_rel);

    /* Find a free slot via CAS loop */
    for (int i = 0; i < FASTKV_MAX_CONCURRENT_TXN; i++) {
        fastkv_ts_t expected = 0;
        if (atomic_compare_exchange_strong_explicit(
                &oracle->slots[i], &expected, ts, memory_order_acq_rel, memory_order_relaxed)) {
            *slot_out = i;
            return ts;
        }
    }

    /* All slots occupied — this should not happen under normal load */
    *slot_out = -1;
    return ts;
}

fastkv_ts_t fastkv_oracle_commit(fastkv_oracle_t *oracle) {
    return atomic_fetch_add_explicit(&oracle->clock, 1, memory_order_acq_rel);
}

fastkv_ts_t fastkv_oracle_now(fastkv_oracle_t *oracle) {
    return atomic_load_explicit(&oracle->clock, memory_order_acquire);
}

void fastkv_oracle_release(fastkv_oracle_t *oracle, int slot) {
    if (slot >= 0 && slot < FASTKV_MAX_CONCURRENT_TXN)
        atomic_store_explicit(&oracle->slots[slot], 0, memory_order_release);
}

fastkv_ts_t fastkv_oracle_min_active(fastkv_oracle_t *oracle) {
    fastkv_ts_t min_ts = atomic_load_explicit(&oracle->clock, memory_order_acquire);

    for (int i = 0; i < FASTKV_MAX_CONCURRENT_TXN; i++) {
        fastkv_ts_t ts = atomic_load_explicit(&oracle->slots[i], memory_order_relaxed);
        if (ts != 0 && ts < min_ts)
            min_ts = ts;
    }
    return min_ts;
}

void fastkv_oracle_advance(fastkv_oracle_t *oracle, fastkv_ts_t at_least) {
    fastkv_ts_t cur;
    do {
        cur = atomic_load_explicit(&oracle->clock, memory_order_acquire);
        if (cur >= at_least)
            return;
    } while (!atomic_compare_exchange_weak_explicit(
        &oracle->clock, &cur, at_least, memory_order_acq_rel, memory_order_relaxed));
}
