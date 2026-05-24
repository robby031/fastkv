#include "compaction.h"

#include "api/kv_api.h"
#include "snapshot.h"
#include "storage/hashtable/ht.h"
#include "ttl.h"
#include "util/log.h"
#include "wal/wal.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

/* How many seconds between background checkpoints */
#define COMPACT_INTERVAL_SEC 30

/* Checkpoint   */

fastkv_err_t fastkv_checkpoint(struct fastkv_db *db) {
    if (!db->wal)
        return FASTKV_OK; /* read-only mode */

    /* 1. Flush WAL to disk */
    fastkv_err_t rc = fastkv_wal_sync(db->wal);
    if (rc != FASTKV_OK)
        return rc;

    /* 2. Assign a checkpoint timestamp = current oracle clock */
    fastkv_ts_t ckpt_ts = fastkv_oracle_now(&db->txn_mgr.oracle);

    /* 3. Write snapshot, simpan nama file yang dihasilkan */
    char snap_name[33];
    rc = fastkv_snapshot_write(db->opts.path, ckpt_ts, db, snap_name, sizeof snap_name);
    if (rc != FASTKV_OK) {
        LOG_ERROR("checkpoint: snapshot write failed");
        return rc;
    }

    /* 4. Rotate WAL so old segments can be trimmed */
    char old_seg[33];
    fastkv_wal_current_segment(db->wal, old_seg, sizeof old_seg);
    rc = fastkv_wal_rotate(db->wal, &db->uuid7);
    if (rc != FASTKV_OK)
        return rc;

    /* 5. Trim WAL segments yang sudah ter-cover snapshot */
    fastkv_wal_trim(db->opts.path, old_seg);

    /* 6. Trim snapshot lama, pertahankan hanya yang terbaru */
    fastkv_snapshot_trim(db->opts.path, snap_name);

    /* 7. hapus kunci yang sudah TTL expired */
    fastkv_ttl_expire(db);

    /* 8. MVCC GC — free versions invisible to all active transactions */
    fastkv_ts_t min_active = fastkv_oracle_min_active(&db->txn_mgr.oracle);
    uint64_t    freed      = fastkv_ht_gc(db->ht, min_active);

    LOG_INFO("checkpoint: ts=%" PRIu64 " min_active=%" PRIu64 " gc_freed=%" PRIu64, ckpt_ts,
        min_active, freed);

    atomic_fetch_add_explicit(&db->stat_snapshot_count, 1, memory_order_relaxed);
    return FASTKV_OK;
}

/* Background thread   */

static void *compaction_thread_fn(void *arg) {
    struct fastkv_db *db = (struct fastkv_db *)arg;

    int elapsed = 0;
    while (!atomic_load_explicit(&db->compact_stop, memory_order_acquire)) {
        struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};
        nanosleep(&ts, NULL);
        elapsed++;

        if (atomic_load_explicit(&db->compact_stop, memory_order_acquire))
            break;

        if (elapsed >= COMPACT_INTERVAL_SEC) {
            elapsed         = 0;
            fastkv_err_t rc = fastkv_checkpoint(db);
            if (rc != FASTKV_OK)
                LOG_WARN("background checkpoint failed: %s", fastkv_strerror(rc));
        }
    }
    return NULL;
}

fastkv_err_t fastkv_compaction_start(struct fastkv_db *db) {
    atomic_store_explicit(&db->compact_stop, false, memory_order_release);
    int err = pthread_create(&db->compact_thread, NULL, compaction_thread_fn, db);
    if (err != 0) {
        LOG_WARN(
            "compaction thread failed to start (errno=%d) — running without background GC", err);
    }
    return FASTKV_OK;
}

void fastkv_compaction_stop(struct fastkv_db *db) {
    atomic_store_explicit(&db->compact_stop, true, memory_order_release);
    pthread_join(db->compact_thread, NULL);
}
