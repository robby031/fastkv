#ifndef FASTKV_INDEX_BTREE_H
#define FASTKV_INDEX_BTREE_H

#include "fastkv/error.h"
#include "fastkv/types.h"

#include <pthread.h>

/* max entri per daun, dan max anak per node internal */
#define BTREE_ORDER 32

typedef struct fastkv_btree fastkv_btree_t;

/* callback untuk scan — kembalikan selain FASTKV_OK untuk berhenti */
typedef fastkv_err_t (*fastkv_btree_scan_cb)(
    fastkv_slice_t key, fastkv_slice_t value, void *udata);

fastkv_err_t fastkv_btree_create(fastkv_btree_t **tree);
void         fastkv_btree_destroy(fastkv_btree_t *tree);

fastkv_err_t fastkv_btree_insert(fastkv_btree_t *tree, fastkv_slice_t key, fastkv_slice_t value);
fastkv_err_t fastkv_btree_delete(fastkv_btree_t *tree, fastkv_slice_t key);
fastkv_err_t fastkv_btree_get(fastkv_btree_t *tree, fastkv_slice_t key, fastkv_slice_t *value_out);

/* scan rentang [min, max] — min atau max boleh FASTKV_SLICE_NULL untuk tanpa batas */
fastkv_err_t fastkv_btree_scan(fastkv_btree_t *tree, fastkv_slice_t min, fastkv_slice_t max,
    fastkv_cursor_dir_t dir, fastkv_btree_scan_cb cb, void *udata);

#endif /* FASTKV_INDEX_BTREE_H */
