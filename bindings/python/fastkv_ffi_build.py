"""
Script build CFFI for FastKV.
Run: python fastkv_ffi_build.py
Or automatically via setup.py.
"""

import os
from cffi import FFI

ffi = FFI()

ffi.cdef("""
    /* error codes */
    typedef enum {
        FASTKV_OK = 0,
        FASTKV_ERR_NOMEM        = -1,
        FASTKV_ERR_INVAL        = -2,
        FASTKV_ERR_IO           = -3,
        FASTKV_ERR_CORRUPT      = -4,
        FASTKV_ERR_FULL         = -5,
        FASTKV_ERR_NOTFOUND     = -10,
        FASTKV_ERR_KEYSIZE      = -11,
        FASTKV_ERR_VALSIZE      = -12,
        FASTKV_ERR_TXN_RO       = -20,
        FASTKV_ERR_TXN_CONFLICT = -21,
        FASTKV_ERR_TXN_CLOSED   = -22,
        FASTKV_ERR_CURSOR_EOF   = -30,
    } fastkv_err_t;

    const char *fastkv_strerror(fastkv_err_t err);

    /* opaque types */
    typedef struct fastkv_db     fastkv_db_t;
    typedef struct fastkv_txn    fastkv_txn_t;
    typedef struct fastkv_cursor fastkv_cursor_t;
    typedef struct fastkv_index  fastkv_index_t;

    /* slice */
    typedef struct {
        const uint8_t *data;
        size_t         len;
    } fastkv_slice_t;

    /* database options */
    typedef struct {
        const char *path;
        size_t      map_size;
        size_t      arena_size;
        bool        sync_writes;
        bool        read_only;
        void       *(*malloc_fn)(size_t);
        void        (*free_fn)(void *);
    } fastkv_opts_t;

    /* cursor direction */
    typedef enum {
        FASTKV_CURSOR_FORWARD  = 0,
        FASTKV_CURSOR_BACKWARD = 1,
    } fastkv_cursor_dir_t;

    /* stats */
    typedef struct {
        uint64_t num_keys;
        uint64_t num_txn_committed;
        uint64_t num_txn_aborted;
        uint64_t num_txn_conflicts;
        uint64_t wal_bytes_written;
        uint64_t snapshot_count;
        uint64_t arena_alloc_bytes;
    } fastkv_stats_t;

    /* callback index */
    typedef int (*fastkv_index_fn)(
        fastkv_slice_t key, fastkv_slice_t value,
        fastkv_slice_t *index_key_out, void *udata);

    typedef fastkv_err_t (*fastkv_index_scan_cb)(
        fastkv_slice_t primary_key, void *udata);

    /* lifecycle */
    fastkv_err_t fastkv_open(fastkv_db_t **db, const fastkv_opts_t *opts);
    fastkv_err_t fastkv_close(fastkv_db_t *db);
    fastkv_err_t fastkv_sync(fastkv_db_t *db);

    /* basic operations */
    fastkv_err_t fastkv_get(fastkv_db_t *db, fastkv_slice_t key,
                            fastkv_slice_t *value_out);
    fastkv_err_t fastkv_put(fastkv_db_t *db, fastkv_slice_t key,
                            fastkv_slice_t value);
    fastkv_err_t fastkv_delete(fastkv_db_t *db, fastkv_slice_t key);
    void         fastkv_free_value(fastkv_db_t *db, fastkv_slice_t *value);

    /* TTL */
    fastkv_err_t fastkv_put_ttl(fastkv_db_t *db, fastkv_slice_t key,
                                fastkv_slice_t value, uint64_t ttl_ms);

    /* transactions */
    fastkv_err_t fastkv_txn_begin(fastkv_db_t *db, bool read_only,
                                  fastkv_txn_t **txn);
    fastkv_err_t fastkv_txn_get(fastkv_txn_t *txn, fastkv_slice_t key,
                                fastkv_slice_t *value_out);
    fastkv_err_t fastkv_txn_put(fastkv_txn_t *txn, fastkv_slice_t key,
                                fastkv_slice_t value);
    fastkv_err_t fastkv_txn_delete(fastkv_txn_t *txn, fastkv_slice_t key);
    fastkv_err_t fastkv_txn_commit(fastkv_txn_t *txn);
    fastkv_err_t fastkv_txn_abort(fastkv_txn_t *txn);

    /* cursor */
    fastkv_err_t fastkv_cursor_open(fastkv_txn_t *txn,
                                    fastkv_cursor_dir_t dir,
                                    fastkv_cursor_t **cursor);
    fastkv_err_t fastkv_cursor_seek(fastkv_cursor_t *cursor,
                                    fastkv_slice_t key);
    fastkv_err_t fastkv_cursor_next(fastkv_cursor_t *cursor);
    fastkv_err_t fastkv_cursor_key(fastkv_cursor_t *cursor,
                                   fastkv_slice_t *key_out);
    fastkv_err_t fastkv_cursor_value(fastkv_cursor_t *cursor,
                                     fastkv_slice_t *value_out);
    void         fastkv_cursor_close(fastkv_cursor_t *cursor);

    /* secondary index */
    fastkv_err_t fastkv_index_create(fastkv_db_t *db, const char *name,
                                     fastkv_index_fn fn, void *udata,
                                     fastkv_index_t **index);
    fastkv_err_t fastkv_index_drop(fastkv_db_t *db, const char *name);
    fastkv_err_t fastkv_index_lookup(fastkv_index_t *index,
                                     fastkv_slice_t index_key,
                                     fastkv_index_scan_cb cb, void *udata);
    fastkv_err_t fastkv_index_range(fastkv_index_t *index,
                                    fastkv_slice_t min_index_key,
                                    fastkv_slice_t max_index_key,
                                    fastkv_index_scan_cb cb, void *udata);
    fastkv_err_t fastkv_json_index_create(fastkv_db_t *db,
                                          const char *index_name,
                                          const char *json_field,
                                          fastkv_index_t **index);

    /* stats */
    fastkv_err_t fastkv_stats(fastkv_db_t *db, fastkv_stats_t *out);
""")

# cari library dan header fastkv
_script_dir = os.path.dirname(os.path.abspath(__file__))
_root = os.path.abspath(os.path.join(_script_dir, "..", ".."))
_include_dir = os.path.join(_root, "include")
_lib_dir = os.path.join(_root, "build", "src")

_lib_file = os.path.join(_lib_dir, "libfastkv.a")

import sys
import platform
print(f"include_dir : {_include_dir}", file=sys.stderr)
print(f"lib_file    : {_lib_file} (exists={os.path.exists(_lib_file)})", file=sys.stderr)

_link_args = ["-lpthread", "-lm"]
if platform.system() == "Linux":
    _link_args.append("-latomic")

ffi.set_source(
    "fastkv._libfastkv",
    '#include "fastkv.h"',
    include_dirs=[_include_dir],
    extra_objects=[_lib_file],
    extra_link_args=_link_args,
)

if __name__ == "__main__":
    ffi.compile(verbose=True)
