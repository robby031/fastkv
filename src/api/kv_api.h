#ifndef FASTKV_API_KV_API_H
#define FASTKV_API_KV_API_H

#include "fastkv/error.h"
#include "fastkv/types.h"

#include "storage/hashtable/ht.h"
#include "txn/txn_manager.h"
#include "wal/wal.h"

#include <pthread.h>
#include <stdatomic.h>

struct fastkv_db {
    fastkv_opts_t    opts;
    fastkv_txn_mgr_t txn_mgr;
    fastkv_ht_t     *ht;
    fastkv_wal_t    *wal;

    /* Secondary indexes (Phase 3) */
    fastkv_index_t  *indexes;
    pthread_rwlock_t index_lock;

    /* Snapshot tracking */
    fastkv_ts_t snapshot_ts; /* ts of last loaded / written snapshot */

    /* Stats */
    _Atomic uint64_t stat_num_keys;
    _Atomic uint64_t stat_snapshot_count;

    /* Background compaction thread */
    pthread_t    compact_thread;
    _Atomic bool compact_stop;
};

/* Internal — called by txn_manager.c */
fastkv_err_t fastkv_db_apply_write_set(
    struct fastkv_db *db, fastkv_write_entry_t *head, fastkv_ts_t commit_ts);

fastkv_err_t fastkv_db_check_conflicts(
    struct fastkv_db *db, fastkv_write_entry_t *head, fastkv_ts_t begin_ts);

#endif /* FASTKV_API_KV_API_H */
