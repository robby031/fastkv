#ifndef FASTKV_H
#define FASTKV_H

/*
 * fastkv.h — public umbrella header
 *
 * Include this single header to access the full FastKV API.
 * Internal headers under include/fastkv/ are not part of the stable ABI.
 */

#include "fastkv/error.h"
#include "fastkv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Database lifecycle   */

fastkv_err_t fastkv_open(fastkv_db_t **db, const fastkv_opts_t *opts);
fastkv_err_t fastkv_close(fastkv_db_t *db);
fastkv_err_t fastkv_sync(fastkv_db_t *db); /* explicit WAL flush + checkpoint */
void         fastkv_compaction_pause(fastkv_db_t *db);  /* hentikan background GC */
void         fastkv_compaction_resume(fastkv_db_t *db); /* hidupkan kembali background GC */

/* Single-operation API (implicit auto-commit transactions)   */

fastkv_err_t fastkv_get(fastkv_db_t *db, fastkv_slice_t key,
    fastkv_slice_t *value_out); /* caller must fastkv_free_value() */

fastkv_err_t fastkv_put(fastkv_db_t *db, fastkv_slice_t key, fastkv_slice_t value);

fastkv_err_t fastkv_delete(fastkv_db_t *db, fastkv_slice_t key);

void fastkv_free_value(fastkv_db_t *db, fastkv_slice_t *value);

/* Explicit transaction API   */

fastkv_err_t fastkv_txn_begin(fastkv_db_t *db, bool read_only, fastkv_txn_t **txn);

fastkv_err_t fastkv_txn_get(fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t *value_out);

fastkv_err_t fastkv_txn_put(fastkv_txn_t *txn, fastkv_slice_t key, fastkv_slice_t value);

fastkv_err_t fastkv_txn_delete(fastkv_txn_t *txn, fastkv_slice_t key);

fastkv_err_t fastkv_txn_commit(fastkv_txn_t *txn);
fastkv_err_t fastkv_txn_abort(fastkv_txn_t *txn);

/* Cursor / range-scan API   */

fastkv_err_t fastkv_cursor_open(
    fastkv_txn_t *txn, fastkv_cursor_dir_t dir, fastkv_cursor_t **cursor);

fastkv_err_t fastkv_cursor_seek(fastkv_cursor_t *cursor, fastkv_slice_t key);
fastkv_err_t fastkv_cursor_next(fastkv_cursor_t *cursor);
fastkv_err_t fastkv_cursor_key(fastkv_cursor_t *cursor, fastkv_slice_t *key_out);
fastkv_err_t fastkv_cursor_value(fastkv_cursor_t *cursor, fastkv_slice_t *value_out);
void         fastkv_cursor_close(fastkv_cursor_t *cursor);

/* Secondary index API */

fastkv_err_t fastkv_index_create(
    fastkv_db_t *db, const char *name, fastkv_index_fn fn, void *udata, fastkv_index_t **index);

fastkv_err_t fastkv_index_drop(fastkv_db_t *db, const char *name);

fastkv_err_t fastkv_index_lookup(
    fastkv_index_t *index, fastkv_slice_t index_key, fastkv_index_scan_cb cb, void *udata);

fastkv_err_t fastkv_index_range(fastkv_index_t *index, fastkv_slice_t min_index_key,
    fastkv_slice_t max_index_key, fastkv_index_scan_cb cb, void *udata);

/* JSON index — buat index berdasarkan field dalam nilai JSON */
fastkv_err_t fastkv_json_index_create(
    fastkv_db_t *db, const char *index_name, const char *json_field, fastkv_index_t **index);

/* TTL API */

/* seperti fastkv_put tapi kunci otomatis dihapus setelah ttl_ms milidetik */
fastkv_err_t fastkv_put_ttl(
    fastkv_db_t *db, fastkv_slice_t key, fastkv_slice_t value, uint64_t ttl_ms);

/* Stats   */

typedef struct {
    uint64_t num_keys;
    uint64_t num_txn_committed;
    uint64_t num_txn_aborted;
    uint64_t num_txn_conflicts;
    uint64_t wal_bytes_written;
    uint64_t snapshot_count;
    uint64_t arena_alloc_bytes;
} fastkv_stats_t;

fastkv_err_t fastkv_stats(fastkv_db_t *db, fastkv_stats_t *out);

/* kontrol level log */
#define FASTKV_LOG_TRACE  0
#define FASTKV_LOG_DEBUG  1
#define FASTKV_LOG_INFO   2
#define FASTKV_LOG_WARN   3
#define FASTKV_LOG_ERROR  4
#define FASTKV_LOG_FATAL  5
#define FASTKV_LOG_SILENT 6

void fastkv_set_log_level(int level);

/* Replikasi */

/* Status satu peer (replica atau primary) */
typedef struct {
    char     addr[64];      /* "host:port" */
    bool     connected;
    uint64_t lag_bytes;     /* byte di belakang primary (sisi replica: primary_offset - received) */
    uint64_t bytes_total;   /* total byte diterima/dikirim */
} fastkv_repl_peer_t;

/* Sisi Primary — mulai server replikasi, replica bisa konek ke port ini */
fastkv_err_t fastkv_repl_serve(fastkv_db_t *db, uint16_t port);
void         fastkv_repl_stop_server(fastkv_db_t *db);

/* Daftar status semua replica yang terhubung ke primary ini.
 * Isi array out[0..cap-1], simpan jumlah di *n_out. */
fastkv_err_t fastkv_repl_peers(fastkv_db_t *db, fastkv_repl_peer_t *out, size_t cap,
    size_t *n_out);

/* Sisi Replica — konek ke primary dan mulai menerima WAL stream */
fastkv_err_t fastkv_repl_connect(fastkv_db_t *db, const char *host, uint16_t port);
void         fastkv_repl_disconnect(fastkv_db_t *db);

/* Status koneksi replica ke primary */
fastkv_repl_peer_t fastkv_repl_primary_stat(fastkv_db_t *db);

#ifdef __cplusplus
}
#endif

#endif /* FASTKV_H */
