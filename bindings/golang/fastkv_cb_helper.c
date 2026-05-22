/*
 * Helper C untuk meneruskan callback index ke Go via cgo.Handle.
 * Fungsi go_index_cb diekspor dari Go menggunakan //export.
 */
#include "fastkv.h"

extern int go_index_scan_cb(fastkv_slice_t pk, void *udata);

static fastkv_err_t wrap_scan_cb(fastkv_slice_t pk, void *udata)
{
    return (fastkv_err_t)go_index_scan_cb(pk, udata);
}

fastkv_err_t fastkv_index_lookup_go(
    fastkv_index_t *idx, fastkv_slice_t ik, void *handle)
{
    return fastkv_index_lookup(idx, ik, wrap_scan_cb, handle);
}

fastkv_err_t fastkv_index_range_go(
    fastkv_index_t *idx,
    fastkv_slice_t min_ik, fastkv_slice_t max_ik,
    void *handle)
{
    return fastkv_index_range(idx, min_ik, max_ik, wrap_scan_cb, handle);
}
