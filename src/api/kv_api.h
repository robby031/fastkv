#ifndef FASTKV_API_KV_API_H
#define FASTKV_API_KV_API_H

#include "fastkv/types.h"
#include "fastkv/error.h"
#include "txn/txn_manager.h"
#include "storage/hashtable/ht.h"
#include "wal/wal.h"

#include <stdatomic.h>
#include <pthread.h>

/*
 * fastkv_db — the top-level database object.
 * Opaque to callers; they only hold a fastkv_db_t* pointer.
 */

struct fastkv_db {
    fastkv_opts_t      opts;
    fastkv_txn_mgr_t   txn_mgr;
    fastkv_ht_t       *ht;
    fastkv_wal_t      *wal;

    /* Secondary indexes (linked list) */
    fastkv_index_t    *indexes;
    pthread_rwlock_t   index_lock;

    /* Stats (lock-free) */
    _Atomic uint64_t   stat_num_keys;

    /* Background compaction thread */
    pthread_t          compact_thread;
    _Atomic bool       compact_stop;
};

/* Called by txn_manager.c — not part of public API */
fastkv_err_t fastkv_db_apply_write_set(struct fastkv_db    *db,
                                        fastkv_write_entry_t *head,
                                        fastkv_ts_t           commit_ts);

fastkv_err_t fastkv_db_check_conflicts(struct fastkv_db    *db,
                                        fastkv_write_entry_t *head,
                                        fastkv_ts_t           begin_ts);

#endif /* FASTKV_API_KV_API_H */
