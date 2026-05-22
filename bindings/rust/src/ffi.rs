//! Raw FFI declarations — manually translated from fastkv.h and fastkv/types.h.
//! Don't use them directly; use safe types in the parent module.

#![allow(non_camel_case_types, dead_code)]

use std::ffi::{c_char, c_void};

#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum fastkv_err_t {
    FASTKV_OK               =  0,
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
}

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum fastkv_cursor_dir_t {
    FASTKV_CURSOR_FORWARD  = 0,
    FASTKV_CURSOR_BACKWARD = 1,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct fastkv_slice_t {
    pub data: *const u8,
    pub len: usize,
}

#[repr(C)]
pub struct fastkv_opts_t {
    pub path: *const c_char,
    pub map_size: usize,
    pub arena_size: usize,
    pub sync_writes: bool,
    pub read_only: bool,
    pub malloc_fn: Option<unsafe extern "C" fn(usize) -> *mut c_void>,
    pub free_fn: Option<unsafe extern "C" fn(*mut c_void)>,
}

#[repr(C)]
pub struct fastkv_stats_t {
    pub num_keys: u64,
    pub num_txn_committed: u64,
    pub num_txn_aborted: u64,
    pub num_txn_conflicts: u64,
    pub wal_bytes_written: u64,
    pub snapshot_count: u64,
    pub arena_alloc_bytes: u64,
}

pub type fastkv_index_fn = Option<
    unsafe extern "C" fn(
        key: fastkv_slice_t,
        value: fastkv_slice_t,
        index_key_out: *mut fastkv_slice_t,
        udata: *mut c_void,
    ) -> i32,
>;

pub type fastkv_index_scan_cb = Option<
    unsafe extern "C" fn(primary_key: fastkv_slice_t, udata: *mut c_void) -> fastkv_err_t,
>;

/* tipe opaque */
#[repr(C)]
pub struct fastkv_db_t { _priv: [u8; 0] }
#[repr(C)]
pub struct fastkv_txn_t { _priv: [u8; 0] }
#[repr(C)]
pub struct fastkv_cursor_t { _priv: [u8; 0] }
#[repr(C)]
pub struct fastkv_index_t { _priv: [u8; 0] }

unsafe extern "C" {
    pub fn fastkv_strerror(err: fastkv_err_t) -> *const c_char;

    /* lifecycle */
    pub fn fastkv_open(db: *mut *mut fastkv_db_t, opts: *const fastkv_opts_t) -> fastkv_err_t;
    pub fn fastkv_close(db: *mut fastkv_db_t) -> fastkv_err_t;
    pub fn fastkv_sync(db: *mut fastkv_db_t) -> fastkv_err_t;

    /* basic operations */
    pub fn fastkv_get(db: *mut fastkv_db_t, key: fastkv_slice_t, value_out: *mut fastkv_slice_t) -> fastkv_err_t;
    pub fn fastkv_put(db: *mut fastkv_db_t, key: fastkv_slice_t, value: fastkv_slice_t) -> fastkv_err_t;
    pub fn fastkv_delete(db: *mut fastkv_db_t, key: fastkv_slice_t) -> fastkv_err_t;
    pub fn fastkv_free_value(db: *mut fastkv_db_t, value: *mut fastkv_slice_t);
    pub fn fastkv_put_ttl(db: *mut fastkv_db_t, key: fastkv_slice_t, value: fastkv_slice_t, ttl_ms: u64) -> fastkv_err_t;

    /* transactions */
    pub fn fastkv_txn_begin(db: *mut fastkv_db_t, read_only: bool, txn: *mut *mut fastkv_txn_t) -> fastkv_err_t;
    pub fn fastkv_txn_get(txn: *mut fastkv_txn_t, key: fastkv_slice_t, value_out: *mut fastkv_slice_t) -> fastkv_err_t;
    pub fn fastkv_txn_put(txn: *mut fastkv_txn_t, key: fastkv_slice_t, value: fastkv_slice_t) -> fastkv_err_t;
    pub fn fastkv_txn_delete(txn: *mut fastkv_txn_t, key: fastkv_slice_t) -> fastkv_err_t;
    pub fn fastkv_txn_commit(txn: *mut fastkv_txn_t) -> fastkv_err_t;
    pub fn fastkv_txn_abort(txn: *mut fastkv_txn_t) -> fastkv_err_t;

    /* cursor */
    pub fn fastkv_cursor_open(txn: *mut fastkv_txn_t, dir: fastkv_cursor_dir_t, cursor: *mut *mut fastkv_cursor_t) -> fastkv_err_t;
    pub fn fastkv_cursor_seek(cursor: *mut fastkv_cursor_t, key: fastkv_slice_t) -> fastkv_err_t;
    pub fn fastkv_cursor_next(cursor: *mut fastkv_cursor_t) -> fastkv_err_t;
    pub fn fastkv_cursor_key(cursor: *mut fastkv_cursor_t, key_out: *mut fastkv_slice_t) -> fastkv_err_t;
    pub fn fastkv_cursor_value(cursor: *mut fastkv_cursor_t, value_out: *mut fastkv_slice_t) -> fastkv_err_t;
    pub fn fastkv_cursor_close(cursor: *mut fastkv_cursor_t);

    /* secondary index */
    pub fn fastkv_index_create(
        db: *mut fastkv_db_t, name: *const c_char,
        fn_: fastkv_index_fn, udata: *mut c_void,
        index: *mut *mut fastkv_index_t,
    ) -> fastkv_err_t;
    pub fn fastkv_index_drop(db: *mut fastkv_db_t, name: *const c_char) -> fastkv_err_t;
    pub fn fastkv_index_lookup(
        index: *mut fastkv_index_t, index_key: fastkv_slice_t,
        cb: fastkv_index_scan_cb, udata: *mut c_void,
    ) -> fastkv_err_t;
    pub fn fastkv_index_range(
        index: *mut fastkv_index_t,
        min_index_key: fastkv_slice_t, max_index_key: fastkv_slice_t,
        cb: fastkv_index_scan_cb, udata: *mut c_void,
    ) -> fastkv_err_t;
    pub fn fastkv_json_index_create(
        db: *mut fastkv_db_t, index_name: *const c_char,
        json_field: *const c_char, index: *mut *mut fastkv_index_t,
    ) -> fastkv_err_t;

    /* stats */
    pub fn fastkv_stats(db: *mut fastkv_db_t, out: *mut fastkv_stats_t) -> fastkv_err_t;
}
