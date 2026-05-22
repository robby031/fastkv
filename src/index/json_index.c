#include "json_index.h"

#include "fastkv.h"

#include "mem/allocator.h"
#include "secondary.h"
#include "util/log.h"

#include <stdlib.h>
#include <string.h>

/* konteks per-index: nama field yang di-ekstrak */
typedef struct {
    char field[128];
} json_ctx_t;

/*
 * Cari nilai dari field JSON di JSON object flat.
 * Mendukung string ("...") dan angka (digit, tanda minus, titik).
 * Kembalikan pointer ke awal nilai dan panjangnya via out_val.
 * Kembalikan true jika ditemukan.
 */
static bool json_extract(
    const char *json, size_t jlen, const char *field, const char **val_start, size_t *val_len) {
    size_t      flen = strlen(field);
    const char *p    = json;
    const char *end  = json + jlen;

    while (p < end) {
        /* cari '"' pembuka nama field */
        while (p < end && *p != '"')
            p++;
        if (p >= end)
            break;
        p++; /* lewat '"' */

        /* bandingkan nama field */
        if ((size_t)(end - p) >= flen + 1 && memcmp(p, field, flen) == 0 && p[flen] == '"') {
            p += flen + 1; /* lewat nama field + '"' penutup */

            /* lewat ':' dan spasi */
            while (p < end && (*p == ':' || *p == ' ' || *p == '\t'))
                p++;
            if (p >= end)
                break;

            if (*p == '"') {
                /* nilai string */
                p++;
                *val_start = p;
                while (p < end && *p != '"') {
                    if (*p == '\\')
                        p++; /* skip escaped char */
                    p++;
                }
                *val_len = (size_t)(p - *val_start);
                return true;
            } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
                /* nilai angka */
                *val_start = p;
                while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == '-' ||
                                      *p == 'e' || *p == 'E' || *p == '+'))
                    p++;
                *val_len = (size_t)(p - *val_start);
                return true;
            } else if (*p == 't' && (size_t)(end - p) >= 4 && memcmp(p, "true", 4) == 0) {
                *val_start = p;
                *val_len   = 4;
                return true;
            } else if (*p == 'f' && (size_t)(end - p) >= 5 && memcmp(p, "false", 5) == 0) {
                *val_start = p;
                *val_len   = 5;
                return true;
            }
            break;
        }

        /* bukan field yang dicari — lewati sampai '"' penutup */
        while (p < end && *p != '"') {
            if (*p == '\\')
                p++;
            p++;
        }
        if (p < end)
            p++; /* lewat '"' penutup */
    }

    return false;
}

/* implementasi fastkv_index_fn untuk JSON index */
static int json_index_fn(
    fastkv_slice_t key, fastkv_slice_t value, fastkv_slice_t *index_key_out, void *udata) {
    (void)key;
    json_ctx_t *ctx = udata;

    if (!value.data || value.len == 0)
        return 1;

    const char *val_start = NULL;
    size_t      val_len   = 0;
    if (!json_extract((const char *)value.data, value.len, ctx->field, &val_start, &val_len))
        return 1;

    *index_key_out = FASTKV_SLICE(val_start, val_len);
    return 0;
}

fastkv_err_t fastkv_json_index_create(
    struct fastkv_db *db, const char *index_name, const char *json_field, fastkv_index_t **out) {
    if (!db || !index_name || !json_field || !out)
        return FASTKV_ERR_INVAL;

    json_ctx_t *ctx = fkv_malloc(sizeof(*ctx));
    if (!ctx)
        return FASTKV_ERR_NOMEM;
    strncpy(ctx->field, json_field, sizeof(ctx->field) - 1);
    ctx->field[sizeof(ctx->field) - 1] = '\0';

    fastkv_err_t rc = fastkv_index_create(db, index_name, json_index_fn, ctx, out);
    if (rc != FASTKV_OK)
        fkv_free(ctx);
    return rc;
}
