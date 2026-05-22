#include "fastkv.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_PUT 0
#define OP_GET 1
#define OP_DELETE 2
#define OP_TXN_COMMIT 3
#define OP_TXN_ABORT 4
#define OP_CHECKPOINT 5
#define OP_RESTART 6
#define OP_CURSOR_SCAN 7
#define OP_COUNT 8

static fastkv_db_t *g_db    = NULL;
static const char  *DB_PATH = "/tmp/fastkv_fuzz_db";

/* Bersihkan dan buat ulang direktori database */
static void bersihkan_db_dir(void) {
    system("rm -rf /tmp/fastkv_fuzz_db");
    system("mkdir -p /tmp/fastkv_fuzz_db");
}

/* Buka ulang database dari nol */
static void buka_db(void) {
    if (g_db) {
        fastkv_close(g_db);
        g_db = NULL;
    }
    bersihkan_db_dir();

    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = DB_PATH;
    opts.sync_writes   = false; /* Matikan sync agar fuzzing lebih cepat */
    opts.map_size      = 1024 * 128;

    if (fastkv_open(&g_db, &opts) != FASTKV_OK) {
        g_db = NULL;
    }
}

/* Inisialisasi dipanggil satu kali saat program pertama dimuat */
__attribute__((constructor)) static void fuzz_init(void) {
    buka_db();
}

/*  Pembaca byte berurutan dari buffer input LibFuzzer */
typedef struct {
    const uint8_t *ptr;
    const uint8_t *end;
} bacaan_t;

static uint8_t baca_u8(bacaan_t *b) {
    if (b->ptr >= b->end)
        return 0;
    return *b->ptr++;
}

/* Baca sekeping data dengan panjang yang ditentukan (zero-copy) */
static const uint8_t *baca_bytes(bacaan_t *b, size_t n) {
    if (b->ptr + n > b->end)
        return NULL;
    const uint8_t *out = b->ptr;
    b->ptr += n;
    return out;
}

/*  Entry point LibFuzzer — dipanggil berulang kali oleh engine */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!g_db || size < 3)
        return 0;

    bacaan_t b = {.ptr = data, .end = data + size};

    /* Ambil jumlah operasi yang akan dieksekusi dari byte pertama */
    uint8_t jumlah_op = baca_u8(&b) % 20 + 1;

    for (uint8_t i = 0; i < jumlah_op && b.ptr < b.end; i++) {
        uint8_t op   = baca_u8(&b) % OP_COUNT;
        uint8_t klen = baca_u8(&b) % 64 + 1; /* Key 1-64 byte */

        const uint8_t *kdata = baca_bytes(&b, klen);
        if (!kdata)
            break;

        fastkv_slice_t key = FASTKV_SLICE(kdata, klen);

        switch (op) {
        case OP_PUT: {
            /* Panjang value 0-127 byte */
            uint8_t        vlen  = baca_u8(&b) % 128;
            const uint8_t *vdata = baca_bytes(&b, vlen);
            if (!vdata)
                break;
            fastkv_slice_t val = FASTKV_SLICE(vdata, vlen);
            fastkv_put(g_db, key, val);
            break;
        }
        case OP_GET: {
            /* Baca key — pastikan tidak crash bila tidak ditemukan */
            fastkv_slice_t val;
            if (fastkv_get(g_db, key, &val) == FASTKV_OK) {
                fastkv_free_value(g_db, &val);
            }
            break;
        }
        case OP_DELETE:
            fastkv_delete(g_db, key);
            break;

        case OP_TXN_COMMIT: {
            /* Transaksi eksplisit: sekuens PUT acak lalu commit */
            fastkv_txn_t *txn = NULL;
            if (fastkv_txn_begin(g_db, false, &txn) != FASTKV_OK)
                break;

            uint8_t langkah = baca_u8(&b) % 5 + 1;
            for (uint8_t s = 0; s < langkah && b.ptr < b.end; s++) {
                uint8_t        vlen  = baca_u8(&b) % 64;
                const uint8_t *vdata = baca_bytes(&b, vlen);
                if (!vdata)
                    break;
                fastkv_slice_t val = FASTKV_SLICE(vdata, vlen);
                fastkv_txn_put(txn, key, val);
            }
            fastkv_txn_commit(txn);
            break;
        }
        case OP_TXN_ABORT: {
            /* Transaksi yang di-abort tidak boleh memengaruhi state */
            fastkv_txn_t *txn = NULL;
            if (fastkv_txn_begin(g_db, false, &txn) != FASTKV_OK)
                break;
            uint8_t        vlen  = baca_u8(&b) % 64;
            const uint8_t *vdata = baca_bytes(&b, vlen);
            if (!vdata) {
                fastkv_txn_abort(txn);
                break;
            }
            fastkv_slice_t val = FASTKV_SLICE(vdata, vlen);
            fastkv_txn_put(txn, key, val);
            fastkv_txn_abort(txn); /* Data TIDAK boleh tersimpan */
            break;
        }
        case OP_CHECKPOINT:
            /* Paksa checkpoint: snapshot + WAL rotate */
            fastkv_sync(g_db);
            break;

        case OP_RESTART: {
            /*
             * Tes crash recovery: tutup db, buka lagi.
             * Data yang sudah di-commit harus masih ada setelah restart.
             */
            fastkv_close(g_db);
            g_db = NULL;

            fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
            opts.path          = DB_PATH;
            opts.sync_writes   = false;
            opts.map_size      = 1024 * 128;

            if (fastkv_open(&g_db, &opts) != FASTKV_OK) {
                /* Jika gagal buka, bersihkan dan mulai lagi dari awal */
                buka_db();
            }
            break;
        }
        case OP_CURSOR_SCAN: {
            /* Buka cursor, scan beberapa entri, pastikan tidak crash */
            fastkv_txn_t *txn = NULL;
            if (fastkv_txn_begin(g_db, true, &txn) != FASTKV_OK)
                break;
            fastkv_cursor_t *cur = NULL;
            if (fastkv_cursor_open(txn, FASTKV_CURSOR_FORWARD, &cur) == FASTKV_OK) {
                uint8_t batas = baca_u8(&b) % 16;
                for (uint8_t c = 0; c < batas; c++) {
                    fastkv_slice_t k, v;
                    if (fastkv_cursor_key(cur, &k) != FASTKV_OK)
                        break;
                    if (fastkv_cursor_value(cur, &v) != FASTKV_OK)
                        break;
                    if (fastkv_cursor_next(cur) != FASTKV_OK)
                        break;
                }
                fastkv_cursor_close(cur);
            }
            fastkv_txn_abort(txn);
            break;
        }
        }
    }

    return 0;
}
