#include "ht.h"

#include "mem/allocator.h"
#include "util/hash.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Helpers */

static size_t bucket_idx(fastkv_ht_t *ht, fastkv_slice_t key) {
    return (size_t)(fastkv_hash(key.data, key.len) & (uint64_t)(ht->capacity - 1));
}

static fastkv_slice_t slice_dup(fastkv_slice_t s) {
    if (s.len == 0)
        return FASTKV_SLICE_NULL;
    uint8_t *buf = fkv_malloc(s.len);
    if (!buf)
        return FASTKV_SLICE_NULL;
    memcpy(buf, s.data, s.len);
    return FASTKV_SLICE(buf, s.len);
}

static bool slice_eq(fastkv_slice_t a, fastkv_slice_t b) {
    return a.len == b.len && memcmp(a.data, b.data, a.len) == 0;
}

/* Lifecycle */

fastkv_err_t fastkv_ht_create(fastkv_ht_t **ht, size_t initial_capacity) {
    assert(initial_capacity > 0 && (initial_capacity & (initial_capacity - 1)) == 0);

    fastkv_ht_t *h = fkv_malloc(sizeof(*h));
    if (!h)
        return FASTKV_ERR_NOMEM;

    h->buckets = fkv_malloc(initial_capacity * sizeof(*h->buckets));
    if (!h->buckets) {
        fkv_free(h);
        return FASTKV_ERR_NOMEM;
    }

    for (size_t i = 0; i < initial_capacity; i++)
        atomic_init(&h->buckets[i], NULL);

    h->capacity = initial_capacity;
    atomic_init(&h->num_keys, 0);
    atomic_init(&h->num_versions, 0);

    *ht = h;
    return FASTKV_OK;
}

void fastkv_ht_destroy(fastkv_ht_t *ht) {
    for (size_t i = 0; i < ht->capacity; i++) {
        fastkv_version_t *v = atomic_load_explicit(&ht->buckets[i], memory_order_relaxed);
        while (v) {
            fastkv_version_t *next = atomic_load_explicit(&v->next, memory_order_relaxed);
            fkv_free((void *)v->key.data);
            if (v->value.data)
                fkv_free((void *)v->value.data);
            fkv_free(v);
            v = next;
        }
    }
    fkv_free(ht->buckets);
    fkv_free(ht);
}

/* Point read   */

fastkv_err_t fastkv_ht_get(
    fastkv_ht_t *ht, fastkv_ts_t snapshot_ts, fastkv_slice_t key, fastkv_slice_t *value_out) {
    size_t            idx = bucket_idx(ht, key);
    fastkv_version_t *v   = atomic_load_explicit(&ht->buckets[idx], memory_order_acquire);

    while (v) {
        if (slice_eq(v->key, key) && v->begin_ts <= snapshot_ts && snapshot_ts < v->end_ts) {
            if (!v->value.data)
                return FASTKV_ERR_NOTFOUND; /* tombstone */
            *value_out = v->value;
            return FASTKV_OK;
        }
        v = atomic_load_explicit(&v->next, memory_order_acquire);
    }
    return FASTKV_ERR_NOTFOUND;
}

/* Point write (insert new version at chain head)   */

static fastkv_err_t ht_insert_version(fastkv_ht_t *ht, fastkv_ts_t commit_ts, fastkv_slice_t key,
    fastkv_slice_t value /* NULL = tombstone */) {
    fastkv_version_t *nv = fkv_malloc(sizeof(*nv));
    if (!nv)
        return FASTKV_ERR_NOMEM;

    nv->begin_ts = commit_ts;
    nv->end_ts   = FASTKV_TS_MAX;
    nv->key      = slice_dup(key);
    nv->value    = value.data ? slice_dup(value) : FASTKV_SLICE_NULL;

    if ((nv->key.data == NULL && key.len > 0) ||
        (value.data && nv->value.data == NULL && value.len > 0)) {
        fkv_free((void *)nv->key.data);
        fkv_free(nv);
        return FASTKV_ERR_NOMEM;
    }

    size_t idx = bucket_idx(ht, key);

    fastkv_version_t *old;
    do {
        old = atomic_load_explicit(&ht->buckets[idx], memory_order_acquire);
        atomic_store_explicit(&nv->next, old, memory_order_relaxed);
        /* Seal the previous head version for the same key */
        fastkv_version_t *prev = old;
        while (prev) {
            if (slice_eq(prev->key, key) && prev->end_ts == FASTKV_TS_MAX) {
                prev->end_ts = commit_ts;
                break;
            }
            prev = atomic_load_explicit(&prev->next, memory_order_relaxed);
        }
    } while (!atomic_compare_exchange_weak_explicit(
        &ht->buckets[idx], &old, nv, memory_order_release, memory_order_relaxed));

    atomic_fetch_add_explicit(&ht->num_versions, 1, memory_order_relaxed);
    return FASTKV_OK;
}

fastkv_err_t fastkv_ht_put(
    fastkv_ht_t *ht, fastkv_ts_t commit_ts, fastkv_slice_t key, fastkv_slice_t value) {
    return ht_insert_version(ht, commit_ts, key, value);
}

fastkv_err_t fastkv_ht_delete(fastkv_ht_t *ht, fastkv_ts_t commit_ts, fastkv_slice_t key) {
    return ht_insert_version(ht, commit_ts, key, FASTKV_SLICE_NULL);
}

/* GC */

uint64_t fastkv_ht_gc(fastkv_ht_t *ht, fastkv_ts_t min_active_ts) {
    uint64_t freed = 0;

    for (size_t i = 0; i < ht->capacity; i++) {
        /* We use a simple approach: rebuild the chain keeping live versions */
        fastkv_version_t *v    = atomic_load_explicit(&ht->buckets[i], memory_order_acquire);
        fastkv_version_t *keep = NULL;

        while (v) {
            fastkv_version_t *next = atomic_load_explicit(&v->next, memory_order_relaxed);
            if (v->end_ts < min_active_ts) {
                fkv_free((void *)v->key.data);
                if (v->value.data)
                    fkv_free((void *)v->value.data);
                fkv_free(v);
                freed++;
            } else {
                atomic_store_explicit(&v->next, keep, memory_order_relaxed);
                keep = v;
            }
            v = next;
        }
        atomic_store_explicit(&ht->buckets[i], keep, memory_order_release);
    }

    atomic_fetch_sub_explicit(&ht->num_versions, freed, memory_order_relaxed);
    return freed;
}
