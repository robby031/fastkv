#include "rtree.h"
#include "mem/allocator.h"

/* Phase 3 stub */
struct fastkv_rtree { uint8_t ndims; };

fastkv_err_t fastkv_rtree_create(fastkv_rtree_t **tree, uint8_t ndims)
{
 *tree = fkv_malloc(sizeof(**tree));
 if (!*tree) return FASTKV_ERR_NOMEM;
 (*tree)->ndims = ndims;
 return FASTKV_OK;
}

void fastkv_rtree_destroy(fastkv_rtree_t *tree) { fkv_free(tree); }

fastkv_err_t fastkv_rtree_insert(fastkv_rtree_t *t, fastkv_rect_t r, fastkv_slice_t k)
{ (void)t;(void)r;(void)k; return FASTKV_OK; }

fastkv_err_t fastkv_rtree_delete(fastkv_rtree_t *t, fastkv_rect_t r, fastkv_slice_t k)
{ (void)t;(void)r;(void)k; return FASTKV_OK; }

fastkv_err_t fastkv_rtree_within(fastkv_rtree_t *t, fastkv_rect_t b, fastkv_rtree_cb cb, void *u)
{ (void)t;(void)b;(void)cb;(void)u; return FASTKV_OK; }

fastkv_err_t fastkv_rtree_intersects(fastkv_rtree_t *t, fastkv_rect_t b, fastkv_rtree_cb cb, void *u)
{ (void)t;(void)b;(void)cb;(void)u; return FASTKV_OK; }

fastkv_err_t fastkv_rtree_nearby(fastkv_rtree_t *t, fastkv_coord_t *pt, uint64_t lim, fastkv_rtree_cb cb, void *u)
{ (void)t;(void)pt;(void)lim;(void)cb;(void)u; return FASTKV_OK; }
