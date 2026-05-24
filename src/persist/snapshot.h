#ifndef FASTKV_PERSIST_SNAPSHOT_H
#define FASTKV_PERSIST_SNAPSHOT_H

#include "fastkv/error.h"
#include "fastkv/types.h"

#include <stddef.h>

/*
 * Snapshot — consistent point-in-time dump of the in-memory state.
 * File naming:  snapshot-<uuid7_hex32>.bin
 * Binary format: header (magic, version, ts, num_keys, crc) + records
 */

#define SNAPSHOT_MAGIC 0x464B5350UL /* "FKSP" */
#define SNAPSHOT_VERSION 1

struct fastkv_db;

/* Tulis snapshot engine saat ini.
 * name_buf (min 33 byte) diisi dengan UUID7 hex dari file yang dibuat. */
fastkv_err_t fastkv_snapshot_write(
    const char *dir, fastkv_ts_t ts, struct fastkv_db *db, char *name_buf, size_t name_cap);

/*
 * Muat snapshot terbaru ke engine.
 * *ts_out diisi timestamp snapshot; caller harus advance oracle clock sebelum
 * replay WAL. Mengembalikan FASTKV_OK dengan *ts_out == 0 jika tidak ada snapshot.
 */
fastkv_err_t fastkv_snapshot_load(const char *dir, struct fastkv_db *db, fastkv_ts_t *ts_out);

/* Hapus snapshot yang namanya (lex) lebih kecil dari keep_name. */
fastkv_err_t fastkv_snapshot_trim(const char *dir, const char *keep_name);

#endif /* FASTKV_PERSIST_SNAPSHOT_H */
