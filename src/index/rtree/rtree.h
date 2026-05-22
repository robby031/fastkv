#ifndef FASTKV_INDEX_RTREE_H
#define FASTKV_INDEX_RTREE_H

#include "fastkv/error.h"
#include "fastkv/types.h"

/*
 * R-tree spatial index — Phase 3.
 *
 * Supports up to RTREE_MAX_DIMS dimensions (DESIGN.md: up to 20).
 * Implements NEARBY, WITHIN, and INTERSECTS queries.
 */

#define RTREE_MAX_DIMS 20

typedef double fastkv_coord_t;

typedef struct {
    fastkv_coord_t min[RTREE_MAX_DIMS];
    fastkv_coord_t max[RTREE_MAX_DIMS];
    uint8_t        ndims;
} fastkv_rect_t;

typedef struct fastkv_rtree fastkv_rtree_t;

fastkv_err_t fastkv_rtree_create(fastkv_rtree_t **tree, uint8_t ndims);
void         fastkv_rtree_destroy(fastkv_rtree_t *tree);

fastkv_err_t fastkv_rtree_insert(fastkv_rtree_t *tree, fastkv_rect_t rect, fastkv_slice_t key);

fastkv_err_t fastkv_rtree_delete(fastkv_rtree_t *tree, fastkv_rect_t rect, fastkv_slice_t key);

typedef fastkv_err_t (*fastkv_rtree_cb)(fastkv_rect_t rect, fastkv_slice_t key, void *udata);

fastkv_err_t fastkv_rtree_within(
    fastkv_rtree_t *tree, fastkv_rect_t bounds, fastkv_rtree_cb cb, void *udata);

fastkv_err_t fastkv_rtree_intersects(
    fastkv_rtree_t *tree, fastkv_rect_t bounds, fastkv_rtree_cb cb, void *udata);

fastkv_err_t fastkv_rtree_nearby(
    fastkv_rtree_t *tree, fastkv_coord_t *point, uint64_t limit, fastkv_rtree_cb cb, void *udata);

#endif /* FASTKV_INDEX_RTREE_H */
