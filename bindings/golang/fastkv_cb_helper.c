/*
* A C helper for passing the index callback to Go via cgo.Handle.
* The handle is passed as a uintptr_t (not void*) to be safe from Go's GC.
*/
#include "fastkv.h"
#include <stdint.h>

extern int go_index_scan_cb(fastkv_slice_t pk, uintptr_t handle);

static fastkv_err_t wrap_scan_cb(fastkv_slice_t pk, void *udata) {
    return (fastkv_err_t)go_index_scan_cb(pk, (uintptr_t)udata);
}

fastkv_err_t fastkv_index_lookup_go(fastkv_index_t *idx, fastkv_slice_t ik, uintptr_t handle) {
    return fastkv_index_lookup(idx, ik, wrap_scan_cb, (void *)handle);
}

fastkv_err_t fastkv_index_range_go(
    fastkv_index_t *idx, fastkv_slice_t min_ik, fastkv_slice_t max_ik, uintptr_t handle) {
    return fastkv_index_range(idx, min_ik, max_ik, wrap_scan_cb, (void *)handle);
}
