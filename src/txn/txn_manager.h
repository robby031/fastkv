#ifndef FASTKV_TXN_MANAGER_H
#define FASTKV_TXN_MANAGER_H

#include "fastkv/types.h"
#include "fastkv/error.h"
#include "txn/mvcc.h"
#include "mem/arena.h"

#include <stdatomic.h>
#include <stdbool.h>

/*
 * Transaction Manager
 *
 * Implements snapshot isolation via MVCC + optimistic write-write conflict
 * detection at commit time.
 *
 * Write-set: each write operation is buffered in a per-txn write-set stored
 * in the arena.  At commit:
 *   1. Assign commit_ts from the oracle.
 *   2. Check write-set for WW conflicts (another txn committed the same key
 *      after our begin_ts).
 *   3. Append WAL records.
 *   4. Apply write-set to the storage engine.
 *   5. Release begin_ts from the oracle.
 */

typedef enum {
    TXN_ACTIVE    = 0,
    TXN_COMMITTED = 1,
    TXN_ABORTED   = 2,
} fastkv_txn_state_t;

/* A single entry in the per-transaction write buffer */
typedef struct fastkv_write_entry {
    struct fastkv_write_entry *next;
    fastkv_slice_t             key;
    fastkv_slice_t             value;   /* .data == NULL → delete */
} fastkv_write_entry_t;

struct fastkv_txn {
    fastkv_ts_t            begin_ts;
    fastkv_ts_t            commit_ts;
    fastkv_txn_state_t     state;
    bool                   read_only;

    fastkv_arena_t        *arena;       /* scratch allocator — freed on commit/abort */
    fastkv_write_entry_t  *write_head;  /* singly-linked write buffer */
    uint32_t               write_count;

    struct fastkv_db      *db;          /* back-pointer */
};

typedef struct fastkv_txn_mgr {
    fastkv_oracle_t     oracle;
    _Atomic uint64_t    num_committed;
    _Atomic uint64_t    num_aborted;
    _Atomic uint64_t    num_conflicts;
} fastkv_txn_mgr_t;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */
fastkv_err_t fastkv_txn_mgr_init(fastkv_txn_mgr_t *mgr);
void         fastkv_txn_mgr_destroy(fastkv_txn_mgr_t *mgr);

/* ── Transaction API (called by db layer) ────────────────────────────────── */
fastkv_err_t fastkv_txn_mgr_begin(fastkv_txn_mgr_t *mgr,
                                   struct fastkv_db  *db,
                                   bool               read_only,
                                   fastkv_txn_t     **txn);

fastkv_err_t fastkv_txn_mgr_commit(fastkv_txn_mgr_t *mgr, fastkv_txn_t *txn);
fastkv_err_t fastkv_txn_mgr_abort(fastkv_txn_mgr_t  *mgr, fastkv_txn_t *txn);

/* ── Write buffer ────────────────────────────────────────────────────────── */
fastkv_err_t fastkv_txn_write_put(fastkv_txn_t   *txn,
                                   fastkv_slice_t  key,
                                   fastkv_slice_t  value);

fastkv_err_t fastkv_txn_write_delete(fastkv_txn_t  *txn,
                                      fastkv_slice_t key);

/* Look up a key in the write buffer first (read-your-own-writes) */
fastkv_err_t fastkv_txn_write_lookup(fastkv_txn_t   *txn,
                                      fastkv_slice_t  key,
                                      fastkv_slice_t *value_out);

#endif /* FASTKV_TXN_MANAGER_H */
