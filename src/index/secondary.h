#ifndef FASTKV_INDEX_SECONDARY_H
#define FASTKV_INDEX_SECONDARY_H

#include "fastkv/error.h"
#include "fastkv/types.h"

#include "index/btree/btree.h"

/*
 * Secondary index — memetakan index_key ke primary_key lewat B+tree.
 *
 * Kunci dalam B+tree adalah composite: index_key + '\x00' + primary_key
 * sehingga semua entri dengan index_key yang sama berurutan di tree dan
 * bisa di-scan dengan range query.
 */
struct fastkv_index {
    char                 name[64];
    fastkv_index_fn      fn;
    void                *udata;
    fastkv_btree_t      *btree;
    struct fastkv_index *next;
};

/* callback untuk hasil pencarian index */
typedef fastkv_err_t (*fastkv_index_scan_cb)(fastkv_slice_t primary_key, void *udata);

/* cari semua primary key dengan index_key tertentu */
fastkv_err_t fastkv_index_lookup(
    fastkv_index_t *idx, fastkv_slice_t index_key, fastkv_index_scan_cb cb, void *udata);

/* scan index pada rentang [min_ik, max_ik] */
fastkv_err_t fastkv_index_range(fastkv_index_t *idx, fastkv_slice_t min_ik, fastkv_slice_t max_ik,
    fastkv_index_scan_cb cb, void *udata);

/* dipanggil oleh kv_api saat ada perubahan kunci */
void secondary_on_put(fastkv_index_t *indexes, fastkv_slice_t primary_key, fastkv_slice_t old_val,
    bool had_old, fastkv_slice_t new_val);

void secondary_on_delete(
    fastkv_index_t *indexes, fastkv_slice_t primary_key, fastkv_slice_t old_val);

#endif /* FASTKV_INDEX_SECONDARY_H */
