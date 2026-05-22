#include "api/kv_api.h"

#include "fastkv.h"

#include "index/btree/btree.h"
#include "index/json_index.h"
#include "index/secondary.h"
#include "mem/allocator.h"
#include "persist/compaction.h"
#include "persist/snapshot.h"
#include "persist/ttl.h"
#include "util/hash.h"
#include "util/log.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* cursor — snapshot dari B+tree saat dibuka */
struct fastkv_cursor {
    fastkv_db_t        *db;
    fastkv_slice_t     *keys;
    fastkv_slice_t     *vals;
    int                 n;
    int                 pos;
    fastkv_cursor_dir_t dir;
};

const char *fastkv_strerror(fastkv_err_t err) {
    switch (err) {
    case FASTKV_OK:
        return "OK";
    case FASTKV_ERR_NOMEM:
        return "out of memory";
    case FASTKV_ERR_INVAL:
        return "invalid argument";
    case FASTKV_ERR_IO:
        return "I/O error";
    case FASTKV_ERR_CORRUPT:
        return "data corruption";
    case FASTKV_ERR_FULL:
        return "capacity full";
    case FASTKV_ERR_NOTFOUND:
        return "key not found";
    case FASTKV_ERR_KEYSIZE:
        return "key too large";
    case FASTKV_ERR_VALSIZE:
        return "value too large";
    case FASTKV_ERR_TXN_RO:
        return "write on read-only transaction";
    case FASTKV_ERR_TXN_CONFLICT:
        return "write-write conflict";
    case FASTKV_ERR_TXN_CLOSED:
        return "transaction already closed";
    case FASTKV_ERR_CURSOR_EOF:
        return "cursor end of range";
    default:
        return "unknown error";
    }
}

/* WAL replay callback — juga isi B+tree */
static fastkv_err_t wal_replay_cb(const fastkv_wal_record_t *rec, void *udata) {
    fastkv_db_t *db = udata;

    if (rec->type == WAL_REC_PUT) {
        fastkv_ht_put(db->ht, rec->ts, rec->key, rec->value);
        fastkv_btree_insert(db->btree, rec->key, rec->value);
    } else if (rec->type == WAL_REC_DELETE) {
        fastkv_ht_delete(db->ht, rec->ts, rec->key);
        fastkv_btree_delete(db->btree, rec->key);
    }
    return FASTKV_OK;
}

/* Open */

fastkv_err_t fastkv_open(fastkv_db_t **db_out, const fastkv_opts_t *opts) {
    if (!db_out || !opts)
        return FASTKV_ERR_INVAL;

    if (opts->malloc_fn)
        fastkv_allocator_init(opts->malloc_fn, opts->free_fn, NULL);

    fastkv_db_t *db = fkv_malloc(sizeof(*db));
    if (!db)
        return FASTKV_ERR_NOMEM;
    memset(db, 0, sizeof(*db));
    db->opts = *opts;
    /* salin path agar DB tidak bergantung pada lifetime buffer pemanggil */
    db->opts.path = strdup(opts->path ? opts->path : ".");

    fastkv_err_t rc;

    if ((rc = fastkv_txn_mgr_init(&db->txn_mgr)) != FASTKV_OK)
        goto fail;

    /* bulatkan map_size ke pangkat dua */
    size_t cap = opts->map_size ? opts->map_size : (1024 * 1024);
    cap--;
    for (size_t i = 1; i < sizeof(cap) * 8; i <<= 1)
        cap |= cap >> i;
    cap++;

    if ((rc = fastkv_ht_create(&db->ht, cap)) != FASTKV_OK)
        goto fail;
    if ((rc = fastkv_btree_create(&db->btree)) != FASTKV_OK)
        goto fail;
    if ((rc = fastkv_ttl_init(db)) != FASTKV_OK)
        goto fail;

    /* recovery: muat snapshot lalu replay WAL */
    fastkv_ts_t snap_ts = 0;
    rc                  = fastkv_snapshot_load(opts->path, db, &snap_ts);
    if (rc != FASTKV_OK && rc != FASTKV_ERR_CORRUPT)
        goto fail;
    db->snapshot_ts = snap_ts;

    fastkv_oracle_advance(&db->txn_mgr.oracle, snap_ts + 1);

    fastkv_ts_t wal_max_ts = 0;
    rc                     = fastkv_wal_replay(opts->path, snap_ts, wal_replay_cb, db, &wal_max_ts);
    if (rc != FASTKV_OK)
        goto fail;

    fastkv_oracle_advance(&db->txn_mgr.oracle, wal_max_ts + 1);

    if (!opts->read_only) {
        rc = fastkv_wal_open(&db->wal, opts->path, opts->sync_writes);
        if (rc != FASTKV_OK)
            goto fail;
    }

    pthread_rwlock_init(&db->index_lock, NULL);
    atomic_init(&db->stat_num_keys, 0);
    atomic_init(&db->stat_snapshot_count, 0);
    atomic_init(&db->compact_stop, false);

    if (!opts->read_only)
        fastkv_compaction_start(db);

    LOG_INFO("fastkv opened (path=%s cap=%zu snap_ts=%" PRIu64 " wal_max_ts=%" PRIu64 ")",
        opts->path, cap, snap_ts, wal_max_ts);

    *db_out = db;
    return FASTKV_OK;

fail:
    fastkv_ttl_destroy(db);
    if (db->btree)
        fastkv_btree_destroy(db->btree);
    if (db->ht)
        fastkv_ht_destroy(db->ht);
    if (db->wal)
        fastkv_wal_close(db->wal);
    fkv_free(db);
    return rc;
}

/* Close */

fastkv_err_t fastkv_close(fastkv_db_t *db) {
    if (!db)
        return FASTKV_OK;

    fastkv_compaction_stop(db);

    if (db->wal)
        fastkv_checkpoint(db);

    fastkv_wal_close(db->wal);
    fastkv_ht_destroy(db->ht);
    fastkv_btree_destroy(db->btree);
    fastkv_ttl_destroy(db);

    /* hapus semua secondary index */
    fastkv_index_t *idx = db->indexes;
    while (idx) {
        fastkv_index_t *next = idx->next;
        fastkv_btree_destroy(idx->btree);
        fkv_free(idx);
        idx = next;
    }

    fastkv_txn_mgr_destroy(&db->txn_mgr);
    pthread_rwlock_destroy(&db->index_lock);
    free((void *)db->opts.path);
    fkv_free(db);
    LOG_INFO("fastkv closed");
    return FASTKV_OK;
}

fastkv_err_t fastkv_sync(fastkv_db_t *db) {
    if (!db)
        return FASTKV_ERR_INVAL;
    return fastkv_checkpoint(db);
}

/* API single-op */

fastkv_err_t fastkv_get(fastkv_db_t *db, fastkv_slice_t key, fastkv_slice_t *value_out) {
    fastkv_txn_t *txn;
    fastkv_err_t  rc = fastkv_txn_begin(db, true, &txn);
    if (rc != FASTKV_OK)
        return rc;
    rc = fastkv_txn_get(txn, key, value_out);
    fastkv_txn_abort(txn);
    return rc;
}

fastkv_err_t fastkv_put(fastkv_db_t *db, fastkv_slice_t key, fastkv_slice_t value) {
    fastkv_txn_t *txn;
    fastkv_err_t  rc = fastkv_txn_begin(db, false, &txn);
    if (rc != FASTKV_OK)
        return rc;
    rc = fastkv_txn_put(txn, key, value);
    if (rc != FASTKV_OK) {
        fastkv_txn_abort(txn);
        return rc;
    }
    return fastkv_txn_commit(txn);
}

fastkv_err_t fastkv_delete(fastkv_db_t *db, fastkv_slice_t key) {
    fastkv_txn_t *txn;
    fastkv_err_t  rc = fastkv_txn_begin(db, false, &txn);
    if (rc != FASTKV_OK)
        return rc;
    rc = fastkv_txn_delete(txn, key);
    if (rc != FASTKV_OK) {
        fastkv_txn_abort(txn);
        return rc;
    }
    return fastkv_txn_commit(txn);
}

void fastkv_free_value(fastkv_db_t *db, fastkv_slice_t *value) {
    (void)db;
    (void)value;
}

/* API transaksi */

fastkv_err_t fastkv_txn_begin(fastkv_db_t *db, bool read_only, fastkv_txn_t **txn) {
    if (!db || !txn)
        return FASTKV_ERR_INVAL;
    if (!read_only && db->opts.read_only)
        return FASTKV_ERR_TXN_RO;
    return fastkv_txn_mgr_begin(&db->txn_mgr, db, read_only, txn);
}

fastkv_err_t fastkv_txn_get(fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t *value_out) {
    if (!txn || !value_out)
        return FASTKV_ERR_INVAL;
    if (txn->state != TXN_ACTIVE)
        return FASTKV_ERR_TXN_CLOSED;
    if (key.len > FASTKV_MAX_KEY_LEN)
        return FASTKV_ERR_KEYSIZE;

    fastkv_err_t rc = fastkv_txn_write_lookup(txn, key, value_out);
    if (rc != FASTKV_ERR_NOTFOUND)
        return rc;

    return fastkv_ht_get(txn->db->ht, txn->begin_ts, key, value_out);
}

fastkv_err_t fastkv_txn_put(fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t value) {
    if (!txn)
        return FASTKV_ERR_INVAL;
    if (key.len > FASTKV_MAX_KEY_LEN)
        return FASTKV_ERR_KEYSIZE;
    if (value.len > FASTKV_MAX_VAL_LEN)
        return FASTKV_ERR_VALSIZE;
    return fastkv_txn_write_put(txn, key, value);
}

fastkv_err_t fastkv_txn_delete(fastkv_txn_t *txn, fastkv_slice_t key) {
    if (!txn)
        return FASTKV_ERR_INVAL;
    if (key.len > FASTKV_MAX_KEY_LEN)
        return FASTKV_ERR_KEYSIZE;
    return fastkv_txn_write_delete(txn, key);
}

fastkv_err_t fastkv_txn_commit(fastkv_txn_t *txn) {
    if (!txn)
        return FASTKV_ERR_INVAL;
    return fastkv_txn_mgr_commit(&txn->db->txn_mgr, txn);
}

fastkv_err_t fastkv_txn_abort(fastkv_txn_t *txn) {
    if (!txn)
        return FASTKV_ERR_INVAL;
    return fastkv_txn_mgr_abort(&txn->db->txn_mgr, txn);
}

/* apply write-set — WAL, HT, dan B+tree */

fastkv_err_t fastkv_db_apply_write_set(
    fastkv_db_t *db, fastkv_write_entry_t *head, fastkv_ts_t commit_ts) {
    for (fastkv_write_entry_t *e = head; e; e = e->next) {
        if (db->wal) {
            fastkv_wal_rec_type_t type = e->value.data ? WAL_REC_PUT : WAL_REC_DELETE;
            fastkv_err_t rc = fastkv_wal_append(db->wal, type, commit_ts, e->key, e->value);
            if (rc != FASTKV_OK)
                return rc;
        }

        if (e->value.data) {
            /* ambil nilai lama untuk update secondary index */
            fastkv_slice_t old_val;
            bool had_old = fastkv_ht_get(db->ht, commit_ts - 1, e->key, &old_val) == FASTKV_OK;

            fastkv_ht_put(db->ht, commit_ts, e->key, e->value);
            fastkv_btree_insert(db->btree, e->key, e->value);
            if (!had_old)
                atomic_fetch_add_explicit(&db->stat_num_keys, 1, memory_order_relaxed);

            pthread_rwlock_rdlock(&db->index_lock);
            secondary_on_put(db->indexes, e->key, old_val, had_old, e->value);
            pthread_rwlock_unlock(&db->index_lock);
        } else {
            fastkv_slice_t old_val;
            bool had_old = fastkv_ht_get(db->ht, commit_ts - 1, e->key, &old_val) == FASTKV_OK;

            fastkv_ht_delete(db->ht, commit_ts, e->key);
            fastkv_btree_delete(db->btree, e->key);
            fastkv_ttl_remove(db, e->key);
            if (had_old)
                atomic_fetch_sub_explicit(&db->stat_num_keys, 1, memory_order_relaxed);

            if (had_old) {
                pthread_rwlock_rdlock(&db->index_lock);
                secondary_on_delete(db->indexes, e->key, old_val);
                pthread_rwlock_unlock(&db->index_lock);
            }
        }
    }
    return FASTKV_OK;
}

/* deteksi konflik write-write */

fastkv_err_t fastkv_db_check_conflicts(
    fastkv_db_t *db, fastkv_write_entry_t *head, fastkv_ts_t begin_ts) {
    fastkv_ht_t *ht = db->ht;

    for (fastkv_write_entry_t *e = head; e; e = e->next) {
        uint64_t h   = fastkv_hash(e->key.data, e->key.len);
        size_t   idx = (size_t)(h & (uint64_t)(ht->capacity - 1));

        fastkv_version_t *v = atomic_load_explicit(&ht->buckets[idx], memory_order_acquire);
        while (v) {
            if (v->key.len == e->key.len && memcmp(v->key.data, e->key.data, e->key.len) == 0) {
                if (v->begin_ts > begin_ts) {
                    LOG_DEBUG("WW conflict: key len=%zu ts=%" PRIu64 " > begin=%" PRIu64,
                        e->key.len, v->begin_ts, begin_ts);
                    return FASTKV_ERR_TXN_CONFLICT;
                }
                break;
            }
            v = atomic_load_explicit(&v->next, memory_order_acquire);
        }
    }
    return FASTKV_OK;
}

/* Cursor — snapshot dari B+tree saat cursor_open */

typedef struct {
    fastkv_slice_t *keys;
    fastkv_slice_t *vals;
    int             n, cap;
} snap_t;

static fastkv_err_t snap_collect(fastkv_slice_t key, fastkv_slice_t val, void *udata) {
    snap_t *s = udata;

    if (s->n == s->cap) {
        int             newcap = s->cap ? s->cap * 2 : 64;
        fastkv_slice_t *nk     = fkv_malloc((size_t)newcap * sizeof(fastkv_slice_t));
        fastkv_slice_t *nv     = fkv_malloc((size_t)newcap * sizeof(fastkv_slice_t));
        if (!nk || !nv) {
            fkv_free(nk);
            fkv_free(nv);
            return FASTKV_ERR_NOMEM;
        }
        if (s->keys) {
            memcpy(nk, s->keys, (size_t)s->n * sizeof(fastkv_slice_t));
            memcpy(nv, s->vals, (size_t)s->n * sizeof(fastkv_slice_t));
            fkv_free(s->keys);
            fkv_free(s->vals);
        }
        s->keys = nk;
        s->vals = nv;
        s->cap  = newcap;
    }

    /* salin key dan val supaya cursor punya salinan stabil */
    uint8_t *kbuf = fkv_malloc(key.len ? key.len : 1);
    uint8_t *vbuf = fkv_malloc(val.len ? val.len : 1);
    if (!kbuf || !vbuf) {
        fkv_free(kbuf);
        fkv_free(vbuf);
        return FASTKV_ERR_NOMEM;
    }
    memcpy(kbuf, key.data, key.len);
    memcpy(vbuf, val.data, val.len);

    s->keys[s->n] = FASTKV_SLICE(kbuf, key.len);
    s->vals[s->n] = FASTKV_SLICE(vbuf, val.len);
    s->n++;
    return FASTKV_OK;
}

fastkv_err_t fastkv_cursor_open(fastkv_txn_t *txn, fastkv_cursor_dir_t dir, fastkv_cursor_t **out) {
    if (!txn || !out)
        return FASTKV_ERR_INVAL;

    fastkv_cursor_t *c = fkv_malloc(sizeof(*c));
    if (!c)
        return FASTKV_ERR_NOMEM;
    memset(c, 0, sizeof(*c));
    c->db  = txn->db;
    c->dir = dir;

    snap_t       s  = {NULL, NULL, 0, 0};
    fastkv_err_t rc = fastkv_btree_scan(txn->db->btree, FASTKV_SLICE_NULL, FASTKV_SLICE_NULL,
        FASTKV_CURSOR_FORWARD, snap_collect, &s);

    if (rc != FASTKV_OK) {
        fkv_free(s.keys);
        fkv_free(s.vals);
        fkv_free(c);
        return rc;
    }

    c->keys = s.keys;
    c->vals = s.vals;
    c->n    = s.n;
    /* posisi awal bergantung arah */
    c->pos = (dir == FASTKV_CURSOR_FORWARD) ? 0 : s.n - 1;

    *out = c;
    return FASTKV_OK;
}

fastkv_err_t fastkv_cursor_seek(fastkv_cursor_t *c, fastkv_slice_t key) {
    if (!c)
        return FASTKV_ERR_INVAL;

    /* binary search di snapshot */
    int lo = 0, hi = c->n;
    while (lo < hi) {
        int    mid = (lo + hi) / 2;
        int    cmp_res;
        size_t n = c->keys[mid].len < key.len ? c->keys[mid].len : key.len;
        cmp_res  = memcmp(c->keys[mid].data, key.data, n);
        if (!cmp_res)
            cmp_res = (c->keys[mid].len > key.len) - (c->keys[mid].len < key.len);
        if (cmp_res < 0)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo >= c->n)
        return FASTKV_ERR_CURSOR_EOF;
    c->pos = lo;
    return FASTKV_OK;
}

fastkv_err_t fastkv_cursor_next(fastkv_cursor_t *c) {
    if (!c)
        return FASTKV_ERR_INVAL;

    if (c->dir == FASTKV_CURSOR_FORWARD) {
        c->pos++;
        if (c->pos >= c->n)
            return FASTKV_ERR_CURSOR_EOF;
    } else {
        c->pos--;
        if (c->pos < 0)
            return FASTKV_ERR_CURSOR_EOF;
    }
    return FASTKV_OK;
}

fastkv_err_t fastkv_cursor_key(fastkv_cursor_t *c, fastkv_slice_t *out) {
    if (!c || !out)
        return FASTKV_ERR_INVAL;
    if (c->pos < 0 || c->pos >= c->n)
        return FASTKV_ERR_CURSOR_EOF;
    *out = c->keys[c->pos];
    return FASTKV_OK;
}

fastkv_err_t fastkv_cursor_value(fastkv_cursor_t *c, fastkv_slice_t *out) {
    if (!c || !out)
        return FASTKV_ERR_INVAL;
    if (c->pos < 0 || c->pos >= c->n)
        return FASTKV_ERR_CURSOR_EOF;
    *out = c->vals[c->pos];
    return FASTKV_OK;
}

void fastkv_cursor_close(fastkv_cursor_t *c) {
    if (!c)
        return;
    for (int i = 0; i < c->n; i++) {
        fkv_free((void *)c->keys[i].data);
        fkv_free((void *)c->vals[i].data);
    }
    fkv_free(c->keys);
    fkv_free(c->vals);
    fkv_free(c);
}

/* secondary index API */

fastkv_err_t fastkv_index_create(
    fastkv_db_t *db, const char *name, fastkv_index_fn fn, void *udata, fastkv_index_t **out) {
    if (!db || !name || !fn || !out)
        return FASTKV_ERR_INVAL;

    fastkv_index_t *idx = fkv_malloc(sizeof(*idx));
    if (!idx)
        return FASTKV_ERR_NOMEM;

    strncpy(idx->name, name, sizeof(idx->name) - 1);
    idx->name[sizeof(idx->name) - 1] = '\0';
    idx->fn                          = fn;
    idx->udata                       = udata;

    fastkv_err_t rc = fastkv_btree_create(&idx->btree);
    if (rc != FASTKV_OK) {
        fkv_free(idx);
        return rc;
    }

    pthread_rwlock_wrlock(&db->index_lock);
    idx->next   = db->indexes;
    db->indexes = idx;
    pthread_rwlock_unlock(&db->index_lock);

    *out = idx;
    LOG_INFO("index '%s' dibuat", name);
    return FASTKV_OK;
}

fastkv_err_t fastkv_index_drop(fastkv_db_t *db, const char *name) {
    if (!db || !name)
        return FASTKV_ERR_INVAL;

    pthread_rwlock_wrlock(&db->index_lock);

    fastkv_index_t **pp = &db->indexes;
    while (*pp) {
        if (strncmp((*pp)->name, name, sizeof((*pp)->name)) == 0) {
            fastkv_index_t *found = *pp;
            *pp                   = found->next;
            pthread_rwlock_unlock(&db->index_lock);
            fastkv_btree_destroy(found->btree);
            fkv_free(found);
            LOG_INFO("index '%s' dihapus", name);
            return FASTKV_OK;
        }
        pp = &(*pp)->next;
    }

    pthread_rwlock_unlock(&db->index_lock);
    return FASTKV_ERR_NOTFOUND;
}

/* TTL API */

fastkv_err_t fastkv_put_ttl(
    fastkv_db_t *db, fastkv_slice_t key, fastkv_slice_t value, uint64_t ttl_ms) {
    if (!db)
        return FASTKV_ERR_INVAL;
    fastkv_err_t rc = fastkv_put(db, key, value);
    if (rc != FASTKV_OK)
        return rc;
    return fastkv_ttl_set(db, key, ttl_ms);
}

/* Stats */

fastkv_err_t fastkv_stats(fastkv_db_t *db, fastkv_stats_t *out) {
    if (!db || !out)
        return FASTKV_ERR_INVAL;
    out->num_keys          = atomic_load(&db->stat_num_keys);
    out->num_txn_committed = atomic_load(&db->txn_mgr.num_committed);
    out->num_txn_aborted   = atomic_load(&db->txn_mgr.num_aborted);
    out->num_txn_conflicts = atomic_load(&db->txn_mgr.num_conflicts);
    out->wal_bytes_written = fastkv_wal_bytes_written(db->wal);
    out->snapshot_count    = atomic_load(&db->stat_snapshot_count);
    out->arena_alloc_bytes = 0;
    return FASTKV_OK;
}
