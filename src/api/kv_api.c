#include "fastkv.h"
#include "api/kv_api.h"
#include "mem/allocator.h"
#include "util/log.h"

#include <string.h>
#include <stdlib.h>

/* strerror */
const char *fastkv_strerror(fastkv_err_t err)
{
 switch (err) {
 case FASTKV_OK: return "OK";
 case FASTKV_ERR_NOMEM: return "out of memory";
 case FASTKV_ERR_INVAL: return "invalid argument";
 case FASTKV_ERR_IO:  return "I/O error";
 case FASTKV_ERR_CORRUPT: return "data corruption";
 case FASTKV_ERR_FULL: return "capacity full";
 case FASTKV_ERR_NOTFOUND: return "key not found";
 case FASTKV_ERR_KEYSIZE: return "key too large";
 case FASTKV_ERR_VALSIZE: return "value too large";
 case FASTKV_ERR_TXN_RO:  return "write on read-only transaction";
 case FASTKV_ERR_TXN_CONFLICT:return "write-write conflict";
 case FASTKV_ERR_TXN_CLOSED:  return "transaction already closed";
 case FASTKV_ERR_CURSOR_EOF:  return "cursor end of range";
 default:  return "unknown error";
 }
}

/* Lifecycle */
fastkv_err_t fastkv_open(fastkv_db_t **db_out, const fastkv_opts_t *opts)
{
 if (!db_out || !opts) return FASTKV_ERR_INVAL;

 if (opts->malloc_fn)
 fastkv_allocator_init(opts->malloc_fn, opts->free_fn, NULL);

 fastkv_db_t *db = fkv_malloc(sizeof(*db));
 if (!db) return FASTKV_ERR_NOMEM;
 memset(db, 0, sizeof(*db));

 db->opts = *opts;

 fastkv_err_t rc;

 if ((rc = fastkv_txn_mgr_init(&db->txn_mgr)) != FASTKV_OK) goto fail;

 size_t cap = opts->map_size;
 if (cap == 0) cap = 1024 * 1024;
 /* Round up to next power of two */
 cap--;
 for (size_t i = 1; i < sizeof(cap) * 8; i <<= 1) cap |= cap >> i;
 cap++;

 if ((rc = fastkv_ht_create(&db->ht, cap)) != FASTKV_OK) goto fail;

 if (!opts->read_only) {
 if ((rc = fastkv_wal_open(&db->wal, opts->path, opts->sync_writes)) != FASTKV_OK)
 goto fail;
 }

 pthread_rwlock_init(&db->index_lock, NULL);
 atomic_init(&db->stat_num_keys, 0);
 atomic_init(&db->compact_stop,  false);

 LOG_INFO("fastkv opened (path=%s, cap=%zu)", opts->path, cap);
 *db_out = db;
 return FASTKV_OK;

fail:
 if (db->ht)  fastkv_ht_destroy(db->ht);
 if (db->wal) fastkv_wal_close(db->wal);
 fkv_free(db);
 return rc;
}

fastkv_err_t fastkv_close(fastkv_db_t *db)
{
 if (!db) return FASTKV_OK;
 atomic_store(&db->compact_stop, true);
 fastkv_wal_sync(db->wal);
 fastkv_wal_close(db->wal);
 fastkv_ht_destroy(db->ht);
 fastkv_txn_mgr_destroy(&db->txn_mgr);
 pthread_rwlock_destroy(&db->index_lock);
 fkv_free(db);
 LOG_INFO("fastkv closed");
 return FASTKV_OK;
}

fastkv_err_t fastkv_sync(fastkv_db_t *db)
{
 if (!db || !db->wal) return FASTKV_ERR_INVAL;
 return fastkv_wal_sync(db->wal);
}

/* Single-op convenience wrappers (auto-commit) */
fastkv_err_t fastkv_get(fastkv_db_t *db, fastkv_slice_t key, fastkv_slice_t *value_out)
{
 fastkv_txn_t *txn;
 fastkv_err_t  rc = fastkv_txn_begin(db, true, &txn);
 if (rc != FASTKV_OK) return rc;

 rc = fastkv_txn_get(txn, key, value_out);
 fastkv_txn_abort(txn);  /* read-only: abort is same as commit */
 return rc;
}

fastkv_err_t fastkv_put(fastkv_db_t *db, fastkv_slice_t key, fastkv_slice_t value)
{
 fastkv_txn_t *txn;
 fastkv_err_t  rc = fastkv_txn_begin(db, false, &txn);
 if (rc != FASTKV_OK) return rc;

 rc = fastkv_txn_put(txn, key, value);
 if (rc != FASTKV_OK) { fastkv_txn_abort(txn); return rc; }
 return fastkv_txn_commit(txn);
}

fastkv_err_t fastkv_delete(fastkv_db_t *db, fastkv_slice_t key)
{
 fastkv_txn_t *txn;
 fastkv_err_t  rc = fastkv_txn_begin(db, false, &txn);
 if (rc != FASTKV_OK) return rc;

 rc = fastkv_txn_delete(txn, key);
 if (rc != FASTKV_OK) { fastkv_txn_abort(txn); return rc; }
 return fastkv_txn_commit(txn);
}

void fastkv_free_value(fastkv_db_t *db, fastkv_slice_t *value)
{
 /* Values returned from get() are owned by the hashtable — do not free */
 (void)db; (void)value;
}

/* Transaction API */
fastkv_err_t fastkv_txn_begin(fastkv_db_t *db, bool read_only, fastkv_txn_t **txn)
{
 if (!db || !txn) return FASTKV_ERR_INVAL;
 if (!read_only && db->opts.read_only) return FASTKV_ERR_TXN_RO;
 return fastkv_txn_mgr_begin(&db->txn_mgr, db, read_only, txn);
}

fastkv_err_t fastkv_txn_get(fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t *value_out)
{
 if (!txn || !value_out) return FASTKV_ERR_INVAL;
 if (txn->state != TXN_ACTIVE) return FASTKV_ERR_TXN_CLOSED;
 if (key.len > FASTKV_MAX_KEY_LEN) return FASTKV_ERR_KEYSIZE;

 /* Read-your-own-writes */
 fastkv_err_t rc = fastkv_txn_write_lookup(txn, key, value_out);
 if (rc != FASTKV_ERR_NOTFOUND) return rc;

 return fastkv_ht_get(txn->db->ht, txn->begin_ts, key, value_out);
}

fastkv_err_t fastkv_txn_put(fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t value)
{
 if (!txn) return FASTKV_ERR_INVAL;
 if (key.len > FASTKV_MAX_KEY_LEN) return FASTKV_ERR_KEYSIZE;
 if (value.len > FASTKV_MAX_VAL_LEN) return FASTKV_ERR_VALSIZE;
 return fastkv_txn_write_put(txn, key, value);
}

fastkv_err_t fastkv_txn_delete(fastkv_txn_t *txn, fastkv_slice_t key)
{
 if (!txn) return FASTKV_ERR_INVAL;
 if (key.len > FASTKV_MAX_KEY_LEN) return FASTKV_ERR_KEYSIZE;
 return fastkv_txn_write_delete(txn, key);
}

fastkv_err_t fastkv_txn_commit(fastkv_txn_t *txn)
{
 if (!txn) return FASTKV_ERR_INVAL;
 return fastkv_txn_mgr_commit(&txn->db->txn_mgr, txn);
}

fastkv_err_t fastkv_txn_abort(fastkv_txn_t *txn)
{
 if (!txn) return FASTKV_ERR_INVAL;
 return fastkv_txn_mgr_abort(&txn->db->txn_mgr, txn);
}

/* Write-set application (called by txn_manager) */
fastkv_err_t fastkv_db_apply_write_set(fastkv_db_t  *db,  fastkv_write_entry_t *head,  fastkv_ts_t commit_ts)
{
 fastkv_err_t rc;
 for (fastkv_write_entry_t *e = head; e; e = e->next) {
 if (db->wal) {
 fastkv_wal_rec_type_t type = e->value.data ? WAL_REC_PUT : WAL_REC_DELETE;
 rc = fastkv_wal_append(db->wal, type, commit_ts, e->key, e->value);
 if (rc != FASTKV_OK) return rc;
 }

 if (e->value.data) {
 rc = fastkv_ht_put(db->ht, commit_ts, e->key, e->value);
 } else {
 rc = fastkv_ht_delete(db->ht, commit_ts, e->key);
 }
 if (rc != FASTKV_OK) return rc;
 }
 return FASTKV_OK;
}

fastkv_err_t fastkv_db_check_conflicts(fastkv_db_t  *db,  fastkv_write_entry_t *head,  fastkv_ts_t begin_ts)
{
 /*
 * For each key in the write set, check whether any version with
 * begin_ts > our begin_ts was committed.  If so, we have a WW conflict.
 *
 * Full implementation requires a conflict table (timestamp -> keys).
 * Phase 1: simplified check — always allow (optimistic, no false-negatives
 * under single-writer workloads; add full check in Phase 2).
 */
 (void)db; (void)head; (void)begin_ts;
 return FASTKV_OK;
}

/* Cursor */
fastkv_err_t fastkv_cursor_open(fastkv_txn_t *txn, fastkv_cursor_dir_t dir, fastkv_cursor_t **cursor)
{
 (void)txn; (void)dir; (void)cursor;
 LOG_WARN("cursor not yet implemented (Phase 3)");
 return FASTKV_ERR_INVAL;
}

fastkv_err_t fastkv_cursor_seek(fastkv_cursor_t *c, fastkv_slice_t key)  { (void)c;(void)key; return FASTKV_ERR_INVAL; }
fastkv_err_t fastkv_cursor_next(fastkv_cursor_t *c)  { (void)c; return FASTKV_ERR_CURSOR_EOF; }
fastkv_err_t fastkv_cursor_key(fastkv_cursor_t *c, fastkv_slice_t *k) { (void)c;(void)k; return FASTKV_ERR_INVAL; }
fastkv_err_t fastkv_cursor_value(fastkv_cursor_t *c, fastkv_slice_t *v)  { (void)c;(void)v; return FASTKV_ERR_INVAL; }
void  fastkv_cursor_close(fastkv_cursor_t *c) { (void)c; }

/* Secondary index API */
fastkv_err_t fastkv_index_create(fastkv_db_t *db, const char *name, fastkv_index_fn fn, void *udata, fastkv_index_t **index)
{
 (void)db;(void)name;(void)fn;(void)udata;(void)index;
 LOG_WARN("index_create not yet implemented (Phase 3)");
 return FASTKV_ERR_INVAL;
}

fastkv_err_t fastkv_index_drop(fastkv_db_t *db, const char *name)
{ (void)db;(void)name; return FASTKV_ERR_INVAL; }

/* Stats */
fastkv_err_t fastkv_stats(fastkv_db_t *db, fastkv_stats_t *out)
{
 if (!db || !out) return FASTKV_ERR_INVAL;
 out->num_keys  = atomic_load(&db->stat_num_keys);
 out->num_txn_committed = atomic_load(&db->txn_mgr.num_committed);
 out->num_txn_aborted = atomic_load(&db->txn_mgr.num_aborted);
 out->num_txn_conflicts = atomic_load(&db->txn_mgr.num_conflicts);
 out->wal_bytes_written = fastkv_wal_bytes_written(db->wal);
 out->snapshot_count = 0;
 out->arena_alloc_bytes = 0;
 return FASTKV_OK;
}
