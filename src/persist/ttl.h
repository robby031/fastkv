#ifndef FASTKV_PERSIST_TTL_H
#define FASTKV_PERSIST_TTL_H

#include "fastkv/error.h"
#include "fastkv/types.h"

struct fastkv_db;

/*
 * TTL expiration — penghapusan otomatis kunci yang sudah kadaluarsa.
 *
 * Dua B+tree dipakai:
 *   ttl_exp:  uint64_be(expiry_ms) + primary_key -> ""   (diurutkan berdasarkan expiry)
 *   ttl_key:  primary_key -> uint64_be(expiry_ms)        (lookup expiry per kunci)
 *
 * Cleanup dipanggil dari compaction checkpoint secara periodik.
 */

fastkv_err_t fastkv_ttl_init(struct fastkv_db *db);
void         fastkv_ttl_destroy(struct fastkv_db *db);

/* set TTL untuk sebuah kunci (dipanggil setelah put ke HT/btree) */
fastkv_err_t fastkv_ttl_set(struct fastkv_db *db, fastkv_slice_t key, uint64_t ttl_ms);

/* hapus TTL entry untuk kunci yang dihapus */
void fastkv_ttl_remove(struct fastkv_db *db, fastkv_slice_t key);

/* hapus semua kunci yang sudah kadaluarsa — dipanggil dari checkpoint */
fastkv_err_t fastkv_ttl_expire(struct fastkv_db *db);

#endif /* FASTKV_PERSIST_TTL_H */
