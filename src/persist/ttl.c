#include "ttl.h"

#include "fastkv.h"

#include "api/kv_api.h"
#include "index/btree/btree.h"
#include "mem/allocator.h"
#include "util/log.h"

#include <string.h>
#include <time.h>

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void u64_to_be(uint64_t v, uint8_t out[8]) {
    for (int i = 7; i >= 0; i--) {
        out[i] = (uint8_t)(v & 0xff);
        v >>= 8;
    }
}

static uint64_t be_to_u64(const uint8_t buf[8]) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | buf[i];
    return v;
}

/* buat kunci expiry: 8 byte be(expiry_ms) + primary_key */
static fastkv_slice_t make_exp_key(uint64_t expiry_ms, fastkv_slice_t pk) {
    size_t   len = 8 + pk.len;
    uint8_t *buf = fkv_malloc(len);
    if (!buf)
        return FASTKV_SLICE_NULL;
    u64_to_be(expiry_ms, buf);
    if (pk.len)
        memcpy(buf + 8, pk.data, pk.len);
    return FASTKV_SLICE(buf, len);
}

fastkv_err_t fastkv_ttl_init(struct fastkv_db *db) {
    fastkv_err_t rc;
    if ((rc = fastkv_btree_create(&db->ttl_exp)) != FASTKV_OK)
        return rc;
    if ((rc = fastkv_btree_create(&db->ttl_key)) != FASTKV_OK) {
        fastkv_btree_destroy(db->ttl_exp);
        db->ttl_exp = NULL;
        return rc;
    }
    return FASTKV_OK;
}

void fastkv_ttl_destroy(struct fastkv_db *db) {
    fastkv_btree_destroy(db->ttl_exp);
    fastkv_btree_destroy(db->ttl_key);
    db->ttl_exp = db->ttl_key = NULL;
}

fastkv_err_t fastkv_ttl_set(struct fastkv_db *db, fastkv_slice_t key, uint64_t ttl_ms) {
    if (!db->ttl_exp)
        return FASTKV_ERR_INVAL;

    uint64_t expiry = now_ms() + ttl_ms;
    uint8_t  exp_be[8];
    u64_to_be(expiry, exp_be);

    /* hapus TTL lama jika ada */
    fastkv_slice_t old_exp;
    if (fastkv_btree_get(db->ttl_key, key, &old_exp) == FASTKV_OK && old_exp.len == 8) {
        uint64_t       old_expiry  = be_to_u64(old_exp.data);
        fastkv_slice_t old_exp_key = make_exp_key(old_expiry, key);
        if (old_exp_key.data) {
            fastkv_btree_delete(db->ttl_exp, old_exp_key);
            fkv_free((void *)old_exp_key.data);
        }
    }

    /* simpan expiry baru */
    fastkv_slice_t exp_val = FASTKV_SLICE(exp_be, 8);
    fastkv_btree_insert(db->ttl_key, key, exp_val);

    fastkv_slice_t exp_key = make_exp_key(expiry, key);
    if (!exp_key.data)
        return FASTKV_ERR_NOMEM;
    fastkv_btree_insert(db->ttl_exp, exp_key, FASTKV_SLICE_NULL);
    fkv_free((void *)exp_key.data);

    return FASTKV_OK;
}

void fastkv_ttl_remove(struct fastkv_db *db, fastkv_slice_t key) {
    if (!db->ttl_exp)
        return;

    fastkv_slice_t old_exp;
    if (fastkv_btree_get(db->ttl_key, key, &old_exp) != FASTKV_OK || old_exp.len != 8)
        return;

    uint64_t       old_expiry = be_to_u64(old_exp.data);
    fastkv_slice_t exp_key    = make_exp_key(old_expiry, key);
    if (exp_key.data) {
        fastkv_btree_delete(db->ttl_exp, exp_key);
        fkv_free((void *)exp_key.data);
    }
    fastkv_btree_delete(db->ttl_key, key);
}

/* callback scan ttl_exp — kumpulkan kunci yang sudah expired */
typedef struct {
    uint64_t now;
    uint8_t  keys[256][FASTKV_MAX_KEY_LEN];
    size_t   klens[256];
    int      n;
} expire_ctx_t;

static fastkv_err_t collect_expired(fastkv_slice_t exp_key, fastkv_slice_t val, void *ud) {
    (void)val;
    expire_ctx_t *ctx = ud;

    if (exp_key.len < 8)
        return FASTKV_OK;
    uint64_t expiry = be_to_u64(exp_key.data);
    if (expiry > ctx->now)
        return FASTKV_ERR_CURSOR_EOF; /* berhenti scan */

    if (ctx->n < 256) {
        size_t klen = exp_key.len - 8;
        if (klen <= FASTKV_MAX_KEY_LEN) {
            memcpy(ctx->keys[ctx->n], exp_key.data + 8, klen);
            ctx->klens[ctx->n] = klen;
            ctx->n++;
        }
    }
    return FASTKV_OK;
}

fastkv_err_t fastkv_ttl_expire(struct fastkv_db *db) {
    if (!db->ttl_exp)
        return FASTKV_OK;

    expire_ctx_t ctx;
    ctx.now = now_ms();
    ctx.n   = 0;

    fastkv_btree_scan(db->ttl_exp, FASTKV_SLICE_NULL, FASTKV_SLICE_NULL, FASTKV_CURSOR_FORWARD,
        collect_expired, &ctx);

    if (ctx.n == 0)
        return FASTKV_OK;

    LOG_DEBUG("TTL: %d kunci expired", ctx.n);

    for (int i = 0; i < ctx.n; i++) {
        fastkv_slice_t pk = FASTKV_SLICE(ctx.keys[i], ctx.klens[i]);

        /* verifikasi expiry masih sesuai (kunci mungkin di-renew atau sudah dihapus) */
        fastkv_slice_t stored_exp;
        if (fastkv_btree_get(db->ttl_key, pk, &stored_exp) == FASTKV_OK && stored_exp.len == 8) {
            uint64_t expiry = be_to_u64(stored_exp.data);
            if (expiry > ctx.now)
                continue; /* sudah di-renew */
        } else {
            continue; /* TTL entry sudah dibersihkan sebelumnya */
        }

        /* hapus dari semua struktur */
        fastkv_delete(db, pk); /* ini juga akan memanggil ttl_remove */
    }

    return FASTKV_OK;
}
