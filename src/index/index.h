#ifndef FASTKV_INDEX_H
#define FASTKV_INDEX_H

#include "fastkv/types.h"
#include "fastkv/error.h"
#include <fastkv.h>

/*
 * Index subsystem — secondary indexes.
 *
 * Concrete implementations:
 * btree/  — ordered B+tree index (range scans, iteration)
 * rtree/  — R-tree spatial index (NEARBY, WITHIN, INTERSECTS)
 * json/ — JSON field extractor + secondary B+tree
 *
 * Each index is backed by a fastkv_index_ops_t vtable so the query planner
 * can dispatch through a uniform interface.
 */

typedef struct fastkv_index_impl fastkv_index_impl_t;

typedef struct {
 fastkv_err_t (*create)(fastkv_index_impl_t **idx, const char *name, void *cfg);
 fastkv_err_t (*destroy)(fastkv_index_impl_t *idx);

 /* Called by txn manager when a committed key/value changes */
 fastkv_err_t (*on_put)(fastkv_index_impl_t *idx, fastkv_ts_t  ts, fastkv_slice_t key, fastkv_slice_t value);

 fastkv_err_t (*on_delete)(fastkv_index_impl_t *idx, fastkv_ts_t  ts, fastkv_slice_t key);

 /* Ordered iteration */
 fastkv_err_t (*iter_range)(fastkv_index_impl_t  *idx, fastkv_ts_t snapshot_ts, fastkv_slice_t min_key, fastkv_slice_t max_key, fastkv_cursor_dir_t dir, fastkv_cursor_t **cursor_out);
} fastkv_index_ops_t;

struct fastkv_index {
 char name[64];
 const fastkv_index_ops_t *ops;
 fastkv_index_impl_t  *impl;
 fastkv_index_fn user_fn; /* custom indexer callback */
 void *udata;
};

/* Built-in index engine descriptors */
extern const fastkv_index_ops_t fastkv_index_btree;
extern const fastkv_index_ops_t fastkv_index_rtree;

#endif /* FASTKV_INDEX_H */
