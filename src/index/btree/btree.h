#ifndef FASTKV_INDEX_BTREE_H
#define FASTKV_INDEX_BTREE_H

#include "fastkv/types.h"
#include "fastkv/error.h"

/*
 * B+tree ordered index.
 *
 * - All data stored in leaf nodes (linked list for O(1) range scan traversal)
 * - Internal nodes hold separator keys only
 * - Node size tuned to a single cache line (64 B) or page (4 KiB)
 * - Not lock-free — protected by a per-tree RW lock for Phase 1
 *   (upgrade to Masstree-style optimistic locking in Phase 3)
 */

#define BTREE_ORDER 32   /* max keys per internal node */

typedef struct fastkv_btree fastkv_btree_t;

fastkv_err_t fastkv_btree_create(fastkv_btree_t **tree);
void         fastkv_btree_destroy(fastkv_btree_t *tree);

fastkv_err_t fastkv_btree_insert(fastkv_btree_t *tree,
                                  fastkv_slice_t  key,
                                  fastkv_slice_t  value);

fastkv_err_t fastkv_btree_delete(fastkv_btree_t *tree, fastkv_slice_t key);

fastkv_err_t fastkv_btree_get(fastkv_btree_t  *tree,
                               fastkv_slice_t   key,
                               fastkv_slice_t  *value_out);

/* Range scan — invokes cb for every key in [min, max] in dir order */
typedef fastkv_err_t (*fastkv_btree_scan_cb)(fastkv_slice_t key,
                                               fastkv_slice_t value,
                                               void          *udata);

fastkv_err_t fastkv_btree_scan(fastkv_btree_t       *tree,
                                fastkv_slice_t        min,
                                fastkv_slice_t        max,
                                fastkv_cursor_dir_t   dir,
                                fastkv_btree_scan_cb  cb,
                                void                 *udata);

#endif /* FASTKV_INDEX_BTREE_H */
