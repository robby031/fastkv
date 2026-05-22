#ifndef FASTKV_TXN_MANAGER_H
#define FASTKV_TXN_MANAGER_H

#include "fastkv/error.h"
#include "fastkv/types.h"

#include "mem/arena.h"
#include "txn/mvcc.h"

#include <stdatomic.h>
#include <stdbool.h>

/*
 * Transaction Manager
 *
 * Snapshot isolation via MVCC + optimistic write-write conflict detection.
 *
 * Commit protocol:
 *   1. Check write-set for WW conflicts against the hashtable.
 *   2. Assign commit_ts from the oracle.
 *   3. Append WAL records (durability before visibility).
 *   4. Apply write-set to the storage engine.
 *   5. Release oracle slot.
 */

typedef enum {
    TXN_ACTIVE    = 0,
    TXN_COMMITTED = 1,
    TXN_ABORTED   = 2,
} fastkv_txn_state_t;

typedef struct fastkv_write_entry {
    struct fastkv_write_entry *next;
    fastkv_slice_t             key;
    fastkv_slice_t             value; /* .data == NULL → delete */
} fastkv_write_entry_t;

struct fastkv_txn {
    fastkv_ts_t        begin_ts;
    fastkv_ts_t        commit_ts;
    fastkv_txn_state_t state;
    bool               read_only;
    int                oracle_slot; /* slot claimed from the oracle */

    fastkv_arena_t       *arena;
    fastkv_write_entry_t *write_head;
    uint32_t              write_count;

    struct fastkv_db *db;
};

typedef struct fastkv_txn_mgr {
    fastkv_oracle_t  oracle;
    _Atomic uint64_t num_committed;
    _Atomic uint64_t num_aborted;
    _Atomic uint64_t num_conflicts;
} fastkv_txn_mgr_t;

/* Lifecycle */
fastkv_err_t fastkv_txn_mgr_init(fastkv_txn_mgr_t *mgr);
void         fastkv_txn_mgr_destroy(fastkv_txn_mgr_t *mgr);

/* Transaction API */
fastkv_err_t fastkv_txn_mgr_begin(
    fastkv_txn_mgr_t *mgr, struct fastkv_db *db, bool read_only, fastkv_txn_t **txn);

fastkv_err_t fastkv_txn_mgr_commit(fastkv_txn_mgr_t *mgr, fastkv_txn_t *txn);
fastkv_err_t fastkv_txn_mgr_abort(fastkv_txn_mgr_t *mgr, fastkv_txn_t *txn);

/* Write buffer */
fastkv_err_t fastkv_txn_write_put(fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t value);

fastkv_err_t fastkv_txn_write_delete(fastkv_txn_t *txn, fastkv_slice_t key);

fastkv_err_t fastkv_txn_write_lookup(
    fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t *value_out);

#endif /* FASTKV_TXN_MANAGER_H */
