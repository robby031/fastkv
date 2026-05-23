#ifndef FASTKV_STORAGE_HT_H
#define FASTKV_STORAGE_HT_H

#include "fastkv/error.h"
#include "fastkv/types.h"

#include <stdatomic.h>

/*
 * Lock-free concurrent hashtable (primary index).
 *
 * Each bucket holds an atomic pointer to a version chain (MVCC singly-linked
 * list).  Readers CAS-load the head and walk the chain to find the visible
 * version for their snapshot timestamp — no lock taken.
 *
 * Writers insert a new version at the head via CAS.  Logical deletion is
 * represented by a tombstone version (value.data == NULL).
 *
 * Resize strategy: incremental split — a background thread doubles the table
 * one segment at a time so there is no global stop-the-world rehash.
 */

/* MVCC version node */
typedef struct fastkv_version {
    struct fastkv_version *_Atomic next; /* next older version of same key */
    fastkv_ts_t          begin_ts;
    _Atomic fastkv_ts_t  end_ts; /* FASTKV_TS_MAX == still live */
    fastkv_slice_t key;    /* owned copy */
    fastkv_slice_t value;  /* owned copy; .data==NULL → tombstone */
} fastkv_version_t;

/* Hashtable */
typedef struct {
    fastkv_version_t *_Atomic *buckets;
    size_t                     capacity; /* must be power of two */
    _Atomic uint64_t           num_keys;
    _Atomic uint64_t           num_versions;
} fastkv_ht_t;

fastkv_err_t fastkv_ht_create(fastkv_ht_t **ht, size_t initial_capacity);
void         fastkv_ht_destroy(fastkv_ht_t *ht);

fastkv_err_t fastkv_ht_get(
    fastkv_ht_t *ht, fastkv_ts_t snapshot_ts, fastkv_slice_t key, fastkv_slice_t *value_out);
fastkv_err_t fastkv_ht_put(
    fastkv_ht_t *ht, fastkv_ts_t commit_ts, fastkv_slice_t key, fastkv_slice_t value);
fastkv_err_t fastkv_ht_delete(fastkv_ht_t *ht, fastkv_ts_t commit_ts, fastkv_slice_t key);

/* GC: remove versions with end_ts < min_active_ts */
uint64_t fastkv_ht_gc(fastkv_ht_t *ht, fastkv_ts_t min_active_ts);

#endif /* FASTKV_STORAGE_HT_H */
