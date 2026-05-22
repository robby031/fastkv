#include "secondary.h"

#include "mem/allocator.h"
#include "util/log.h"

#include <string.h>

/*
 * Kunci dalam B+tree: index_key + '\x00' + primary_key (composite)
 * Nilai yang disimpan: primary_key itu sendiri
 *
 * Dengan begini, scan range bisa langsung membaca primary key dari nilai,
 * tanpa perlu mem-parse composite key lagi.
 */
static fastkv_slice_t make_composite(fastkv_slice_t ik, fastkv_slice_t pk) {
    size_t   len = ik.len + 1 + pk.len;
    uint8_t *buf = fkv_malloc(len);
    if (!buf)
        return FASTKV_SLICE_NULL;
    if (ik.len)
        memcpy(buf, ik.data, ik.len);
    buf[ik.len] = '\x00';
    if (pk.len)
        memcpy(buf + ik.len + 1, pk.data, pk.len);
    return FASTKV_SLICE(buf, len);
}

/* buat salinan null-terminated dari slice agar callback aman pakai strstr/strtok */
static fastkv_slice_t nt_copy(fastkv_slice_t s) {
    uint8_t *buf = fkv_malloc(s.len + 1);
    if (!buf)
        return FASTKV_SLICE_NULL;
    if (s.len)
        memcpy(buf, s.data, s.len);
    buf[s.len] = '\0';
    return FASTKV_SLICE(buf, s.len);
}

static void remove_index_entry(
    fastkv_btree_t *btree, fastkv_index_fn fn, void *udata, fastkv_slice_t pk, fastkv_slice_t val) {
    /* salinan null-terminated agar callback aman pakai strstr */
    fastkv_slice_t safe = nt_copy(val);
    if (!safe.data && val.len)
        return;
    fastkv_slice_t ik   = FASTKV_SLICE_NULL;
    int            skip = fn(pk, safe, &ik, udata);
    if (skip == 0 && ik.data) {
        /* ik.data mungkin menunjuk ke dalam safe — buat composite dulu baru bebaskan */
        fastkv_slice_t comp = make_composite(ik, pk);
        fkv_free((void *)safe.data);
        if (comp.data) {
            fastkv_btree_delete(btree, comp);
            fkv_free((void *)comp.data);
        }
    } else {
        fkv_free((void *)safe.data);
    }
}

static void add_index_entry(
    fastkv_btree_t *btree, fastkv_index_fn fn, void *udata, fastkv_slice_t pk, fastkv_slice_t val) {
    fastkv_slice_t safe = nt_copy(val);
    if (!safe.data && val.len)
        return;
    fastkv_slice_t ik   = FASTKV_SLICE_NULL;
    int            skip = fn(pk, safe, &ik, udata);
    if (skip == 0 && ik.data) {
        fastkv_slice_t comp = make_composite(ik, pk);
        fkv_free((void *)safe.data);
        if (comp.data) {
            fastkv_btree_insert(btree, comp, pk);
            fkv_free((void *)comp.data);
        }
    } else {
        fkv_free((void *)safe.data);
    }
}

void secondary_on_put(fastkv_index_t *indexes, fastkv_slice_t pk, fastkv_slice_t old_val,
    bool had_old, fastkv_slice_t new_val) {
    for (fastkv_index_t *idx = indexes; idx; idx = idx->next) {
        if (had_old)
            remove_index_entry(idx->btree, idx->fn, idx->udata, pk, old_val);
        add_index_entry(idx->btree, idx->fn, idx->udata, pk, new_val);
    }
}

void secondary_on_delete(fastkv_index_t *indexes, fastkv_slice_t pk, fastkv_slice_t old_val) {
    for (fastkv_index_t *idx = indexes; idx; idx = idx->next)
        remove_index_entry(idx->btree, idx->fn, idx->udata, pk, old_val);
}

/* scan callback: nilai dalam B+tree adalah primary key, teruskan ke callback pengguna */
typedef struct {
    fastkv_index_scan_cb cb;
    void                *udata;
} fwd_ctx_t;

static fastkv_err_t fwd_cb(fastkv_slice_t key, fastkv_slice_t val, void *ud) {
    (void)key;
    fwd_ctx_t *ctx = ud;
    return ctx->cb(val, ctx->udata);
}

fastkv_err_t fastkv_index_lookup(
    fastkv_index_t *idx, fastkv_slice_t ik, fastkv_index_scan_cb cb, void *udata) {
    if (!idx || !cb)
        return FASTKV_ERR_INVAL;

    /* batas: ik+'\x00' sampai ik+'\x01' — menangkap semua primary key untuk ik */
    uint8_t *lobuf = fkv_malloc(ik.len + 1);
    uint8_t *hibuf = fkv_malloc(ik.len + 1);
    if (!lobuf || !hibuf) {
        fkv_free(lobuf);
        fkv_free(hibuf);
        return FASTKV_ERR_NOMEM;
    }
    if (ik.len) {
        memcpy(lobuf, ik.data, ik.len);
        memcpy(hibuf, ik.data, ik.len);
    }
    lobuf[ik.len] = '\x00';
    hibuf[ik.len] = '\x01';

    fwd_ctx_t    ctx = {cb, udata};
    fastkv_err_t rc  = fastkv_btree_scan(idx->btree, FASTKV_SLICE(lobuf, ik.len + 1),
        FASTKV_SLICE(hibuf, ik.len + 1), FASTKV_CURSOR_FORWARD, fwd_cb, &ctx);

    fkv_free(lobuf);
    fkv_free(hibuf);
    return rc;
}

fastkv_err_t fastkv_index_range(fastkv_index_t *idx, fastkv_slice_t min_ik, fastkv_slice_t max_ik,
    fastkv_index_scan_cb cb, void *udata) {
    if (!idx || !cb)
        return FASTKV_ERR_INVAL;

    uint8_t *lobuf = fkv_malloc(min_ik.len + 1);
    uint8_t *hibuf = fkv_malloc(max_ik.len + 1);
    if (!lobuf || !hibuf) {
        fkv_free(lobuf);
        fkv_free(hibuf);
        return FASTKV_ERR_NOMEM;
    }
    if (min_ik.len)
        memcpy(lobuf, min_ik.data, min_ik.len);
    lobuf[min_ik.len] = '\x00';
    if (max_ik.len)
        memcpy(hibuf, max_ik.data, max_ik.len);
    hibuf[max_ik.len] = '\x01';

    fwd_ctx_t    ctx = {cb, udata};
    fastkv_err_t rc  = fastkv_btree_scan(idx->btree, FASTKV_SLICE(lobuf, min_ik.len + 1),
        FASTKV_SLICE(hibuf, max_ik.len + 1), FASTKV_CURSOR_FORWARD, fwd_cb, &ctx);

    fkv_free(lobuf);
    fkv_free(hibuf);
    return rc;
}
