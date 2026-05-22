#include "btree.h"
#include "mem/allocator.h"
#include "util/log.h"

/* Phase 1 stub — real implementation in Phase 2/3 */
struct fastkv_btree {
 int _placeholder;
};

fastkv_err_t fastkv_btree_create(fastkv_btree_t **tree)
{
 *tree = fkv_malloc(sizeof(**tree));
 return *tree ? FASTKV_OK : FASTKV_ERR_NOMEM;
}

void fastkv_btree_destroy(fastkv_btree_t *tree) { fkv_free(tree); }

fastkv_err_t fastkv_btree_insert(fastkv_btree_t *tree, fastkv_slice_t key, fastkv_slice_t value)
{
 (void)tree; (void)key; (void)value;
 LOG_WARN("btree_insert: not yet implemented");
 return FASTKV_OK;
}

fastkv_err_t fastkv_btree_delete(fastkv_btree_t *tree, fastkv_slice_t key)
{
 (void)tree; (void)key;
 return FASTKV_OK;
}

fastkv_err_t fastkv_btree_get(fastkv_btree_t *tree, fastkv_slice_t key, fastkv_slice_t *out)
{
 (void)tree; (void)key; (void)out;
 return FASTKV_ERR_NOTFOUND;
}

fastkv_err_t fastkv_btree_scan(fastkv_btree_t *tree, fastkv_slice_t min, fastkv_slice_t max, fastkv_cursor_dir_t dir, fastkv_btree_scan_cb cb, void *udata)
{
 (void)tree; (void)min; (void)max; (void)dir; (void)cb; (void)udata;
 return FASTKV_OK;
}
