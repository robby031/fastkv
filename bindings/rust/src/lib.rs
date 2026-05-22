//! FastKV Rust binding — antarmuka aman (safe) di atas C FFI.
//!
//! # Contoh
//! ```no_run
//! use fastkv::{DB, Opts};
//!
//! let db = DB::open("/tmp/mydb", Opts::default()).unwrap();
//! db.put(b"kunci", b"nilai").unwrap();
//! let val = db.get(b"kunci").unwrap();
//! assert_eq!(val, b"nilai");
//! ```

mod ffi;

pub use error::{Error, Result};

mod error {
    use crate::ffi;
    use std::ffi::CStr;

    #[derive(Debug, Clone, PartialEq, Eq)]
    pub enum Error {
        NotFound,
        Conflict,
        ReadOnly,
        CursorEOF,
        Other(i32, String),
    }

    impl std::fmt::Display for Error {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            match self {
                Error::NotFound => write!(f, "kunci tidak ditemukan"),
                Error::Conflict => write!(f, "konflik transaksi write-write"),
                Error::ReadOnly => write!(f, "write pada transaksi read-only"),
                Error::CursorEOF => write!(f, "cursor sudah habis"),
                Error::Other(_, msg) => write!(f, "{}", msg),
            }
        }
    }

    impl std::error::Error for Error {}

    pub type Result<T> = std::result::Result<T, Error>;

    pub(crate) fn check(rc: ffi::fastkv_err_t) -> Result<()> {
        use ffi::fastkv_err_t::*;
        match rc {
            FASTKV_OK => Ok(()),
            FASTKV_ERR_NOTFOUND => Err(Error::NotFound),
            FASTKV_ERR_TXN_CONFLICT => Err(Error::Conflict),
            FASTKV_ERR_TXN_RO => Err(Error::ReadOnly),
            FASTKV_ERR_CURSOR_EOF => Err(Error::CursorEOF),
            other => {
                let msg = unsafe {
                    CStr::from_ptr(ffi::fastkv_strerror(other))
                        .to_string_lossy()
                        .into_owned()
                };
                Err(Error::Other(other as i32, msg))
            }
        }
    }
}

fn to_slice(data: &[u8]) -> ffi::fastkv_slice_t {
    ffi::fastkv_slice_t {
        data: if data.is_empty() {
            std::ptr::null()
        } else {
            data.as_ptr()
        },
        len: data.len(),
    }
}

fn from_slice(s: &ffi::fastkv_slice_t) -> Vec<u8> {
    if s.data.is_null() || s.len == 0 {
        return Vec::new();
    }
    unsafe { std::slice::from_raw_parts(s.data, s.len).to_vec() }
}

/// Opsi pembukaan database.
pub struct Opts {
    pub map_size: usize,
    pub arena_size: usize,
    pub sync_writes: bool,
    pub read_only: bool,
}

impl Default for Opts {
    fn default() -> Self {
        Opts {
            map_size: 1024 * 1024,
            arena_size: 64 * 1024,
            sync_writes: true,
            read_only: false,
        }
    }
}

/// Handle utama database FastKV.
/// Aman dikirim antar thread (`Send + Sync`).
pub struct DB {
    ptr: *mut ffi::fastkv_db_t,
}

unsafe impl Send for DB {}
unsafe impl Sync for DB {}

impl DB {
    /// Buka atau buat database di path yang diberikan.
    pub fn open(path: &str, opts: Opts) -> Result<Self> {
        use std::ffi::CString;
        let cpath = CString::new(path).expect("path tidak valid");

        let copts = ffi::fastkv_opts_t {
            path: cpath.as_ptr(),
            map_size: opts.map_size,
            arena_size: opts.arena_size,
            sync_writes: opts.sync_writes,
            read_only: opts.read_only,
            malloc_fn: None,
            free_fn: None,
        };

        let mut ptr = std::ptr::null_mut();
        error::check(unsafe { ffi::fastkv_open(&mut ptr, &copts) })?;
        Ok(DB { ptr })
    }

    /// Baca nilai untuk kunci yang diberikan.
    pub fn get(&self, key: &[u8]) -> Result<Vec<u8>> {
        let mut s = ffi::fastkv_slice_t { data: std::ptr::null(), len: 0 };
        error::check(unsafe { ffi::fastkv_get(self.ptr, to_slice(key), &mut s) })?;
        let val = from_slice(&s);
        unsafe { ffi::fastkv_free_value(self.ptr, &mut s) };
        Ok(val)
    }

    /// Simpan pasangan kunci-nilai.
    pub fn put(&self, key: &[u8], value: &[u8]) -> Result<()> {
        error::check(unsafe { ffi::fastkv_put(self.ptr, to_slice(key), to_slice(value)) })
    }

    /// Simpan kunci dengan TTL dalam milidetik.
    pub fn put_ttl(&self, key: &[u8], value: &[u8], ttl_ms: u64) -> Result<()> {
        error::check(unsafe {
            ffi::fastkv_put_ttl(self.ptr, to_slice(key), to_slice(value), ttl_ms)
        })
    }

    /// Hapus kunci dari database.
    pub fn delete(&self, key: &[u8]) -> Result<()> {
        error::check(unsafe { ffi::fastkv_delete(self.ptr, to_slice(key)) })
    }

    /// Flush WAL dan jalankan checkpoint.
    pub fn sync(&self) -> Result<()> {
        error::check(unsafe { ffi::fastkv_sync(self.ptr) })
    }

    /// Kembalikan statistik operasional.
    pub fn stats(&self) -> Result<Stats> {
        let mut s = ffi::fastkv_stats_t {
            num_keys: 0,
            num_txn_committed: 0,
            num_txn_aborted: 0,
            num_txn_conflicts: 0,
            wal_bytes_written: 0,
            snapshot_count: 0,
            arena_alloc_bytes: 0,
        };
        error::check(unsafe { ffi::fastkv_stats(self.ptr, &mut s) })?;
        Ok(Stats {
            num_keys: s.num_keys,
            num_txn_committed: s.num_txn_committed,
            num_txn_aborted: s.num_txn_aborted,
            num_txn_conflicts: s.num_txn_conflicts,
            wal_bytes_written: s.wal_bytes_written,
            snapshot_count: s.snapshot_count,
            arena_alloc_bytes: s.arena_alloc_bytes,
        })
    }

    /// Mulai transaksi baru.
    pub fn begin(&self, read_only: bool) -> Result<Txn<'_>> {
        let mut ptr = std::ptr::null_mut();
        error::check(unsafe { ffi::fastkv_txn_begin(self.ptr, read_only, &mut ptr) })?;
        Ok(Txn { db: self, ptr, done: false })
    }

    /// Buat index sekunder berbasis field JSON.
    pub fn json_index<'a>(&'a self, name: &str, field: &str) -> Result<Index<'a>> {
        use std::ffi::CString;
        let cname = CString::new(name).unwrap();
        let cfield = CString::new(field).unwrap();
        let mut ptr = std::ptr::null_mut();
        error::check(unsafe {
            ffi::fastkv_json_index_create(self.ptr, cname.as_ptr(), cfield.as_ptr(), &mut ptr)
        })?;
        Ok(Index { db: self, ptr, name: name.to_owned() })
    }

    /// Hapus index dengan nama yang diberikan.
    pub fn drop_index(&self, name: &str) -> Result<()> {
        use std::ffi::CString;
        let cname = CString::new(name).unwrap();
        error::check(unsafe { ffi::fastkv_index_drop(self.ptr, cname.as_ptr()) })
    }
}

impl Drop for DB {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { ffi::fastkv_close(self.ptr) };
        }
    }
}

/// Statistik operasional database.
#[derive(Debug, Clone)]
pub struct Stats {
    pub num_keys: u64,
    pub num_txn_committed: u64,
    pub num_txn_aborted: u64,
    pub num_txn_conflicts: u64,
    pub wal_bytes_written: u64,
    pub snapshot_count: u64,
    pub arena_alloc_bytes: u64,
}

/// Transaksi eksplisit FastKV.
pub struct Txn<'db> {
    db: &'db DB,
    ptr: *mut ffi::fastkv_txn_t,
    done: bool,
}

impl<'db> Txn<'db> {
    /// Baca kunci dalam transaksi ini.
    pub fn get(&self, key: &[u8]) -> Result<Vec<u8>> {
        let mut s = ffi::fastkv_slice_t { data: std::ptr::null(), len: 0 };
        error::check(unsafe { ffi::fastkv_txn_get(self.ptr, to_slice(key), &mut s) })?;
        Ok(from_slice(&s))
    }

    /// Simpan kunci-nilai dalam transaksi ini.
    pub fn put(&self, key: &[u8], value: &[u8]) -> Result<()> {
        error::check(unsafe { ffi::fastkv_txn_put(self.ptr, to_slice(key), to_slice(value)) })
    }

    /// Hapus kunci dalam transaksi ini.
    pub fn delete(&self, key: &[u8]) -> Result<()> {
        error::check(unsafe { ffi::fastkv_txn_delete(self.ptr, to_slice(key)) })
    }

    /// Commit transaksi.
    pub fn commit(mut self) -> Result<()> {
        self.done = true;
        error::check(unsafe { ffi::fastkv_txn_commit(self.ptr) })
    }

    /// Buka cursor pada transaksi ini.
    pub fn cursor(&self, forward: bool) -> Result<Cursor<'_>> {
        use ffi::fastkv_cursor_dir_t::*;
        let dir = if forward { FASTKV_CURSOR_FORWARD } else { FASTKV_CURSOR_BACKWARD };
        let mut ptr = std::ptr::null_mut();
        error::check(unsafe { ffi::fastkv_cursor_open(self.ptr, dir, &mut ptr) })?;
        Ok(Cursor { ptr, exhausted: false, _txn: std::marker::PhantomData })
    }
}

impl Drop for Txn<'_> {
    fn drop(&mut self) {
        if !self.done && !self.ptr.is_null() {
            unsafe { ffi::fastkv_txn_abort(self.ptr) };
        }
    }
}

/// Cursor berurutan atas snapshot B+tree.
pub struct Cursor<'txn> {
    ptr: *mut ffi::fastkv_cursor_t,
    exhausted: bool,
    _txn: std::marker::PhantomData<&'txn ()>,
}

impl<'txn> Cursor<'txn> {
    /// Posisikan cursor ke kunci >= key.
    pub fn seek(&mut self, key: &[u8]) -> Result<()> {
        error::check(unsafe { ffi::fastkv_cursor_seek(self.ptr, to_slice(key)) })
    }

    /// Kunci pada posisi saat ini.
    pub fn key(&self) -> Result<Vec<u8>> {
        let mut s = ffi::fastkv_slice_t { data: std::ptr::null(), len: 0 };
        error::check(unsafe { ffi::fastkv_cursor_key(self.ptr, &mut s) })?;
        Ok(from_slice(&s))
    }

    /// Nilai pada posisi saat ini.
    pub fn value(&self) -> Result<Vec<u8>> {
        let mut s = ffi::fastkv_slice_t { data: std::ptr::null(), len: 0 };
        error::check(unsafe { ffi::fastkv_cursor_value(self.ptr, &mut s) })?;
        Ok(from_slice(&s))
    }

    /// Maju ke entri berikutnya. Kembalikan false jika sudah habis.
    pub fn next(&mut self) -> Result<bool> {
        if self.exhausted {
            return Ok(false);
        }
        let rc = unsafe { ffi::fastkv_cursor_next(self.ptr) };
        if rc == ffi::fastkv_err_t::FASTKV_ERR_CURSOR_EOF {
            self.exhausted = true;
            return Ok(false);
        }
        error::check(rc)?;
        Ok(true)
    }

    /// Iterator atas pasangan (kunci, nilai).
    pub fn iter(&mut self) -> CursorIter<'_, 'txn> {
        CursorIter { cursor: self, first: true }
    }
}

impl Drop for Cursor<'_> {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { ffi::fastkv_cursor_close(self.ptr) };
        }
    }
}

/// Iterator yang menghasilkan (kunci, nilai) dari cursor.
pub struct CursorIter<'c, 'txn> {
    cursor: &'c mut Cursor<'txn>,
    first: bool,
}

impl<'c, 'txn> Iterator for CursorIter<'c, 'txn> {
    type Item = Result<(Vec<u8>, Vec<u8>)>;

    fn next(&mut self) -> Option<Self::Item> {
        if !self.first {
            match self.cursor.next() {
                Ok(false) => return None,
                Err(e) => return Some(Err(e)),
                Ok(true) => {}
            }
        }
        self.first = false;

        let k = match self.cursor.key() {
            Ok(k) => k,
            Err(e) => return Some(Err(e)),
        };
        let v = match self.cursor.value() {
            Ok(v) => v,
            Err(e) => return Some(Err(e)),
        };
        Some(Ok((k, v)))
    }
}

/// Handle index sekunder FastKV.
pub struct Index<'db> {
    db: &'db DB,
    ptr: *mut ffi::fastkv_index_t,
    name: String,
}

impl<'db> Index<'db> {
    /// Cari semua primary key yang cocok dengan index_key.
    pub fn lookup(&self, index_key: &[u8]) -> Result<Vec<Vec<u8>>> {
        let mut hasil: Vec<Vec<u8>> = Vec::new();
        let hasil_ptr = &mut hasil as *mut Vec<Vec<u8>> as *mut std::ffi::c_void;

        unsafe extern "C" fn cb(pk: ffi::fastkv_slice_t, udata: *mut std::ffi::c_void)
            -> ffi::fastkv_err_t
        {
            let hasil = &mut *(udata as *mut Vec<Vec<u8>>);
            if !pk.data.is_null() && pk.len > 0 {
                hasil.push(std::slice::from_raw_parts(pk.data, pk.len).to_vec());
            }
            ffi::fastkv_err_t::FASTKV_OK
        }

        error::check(unsafe {
            ffi::fastkv_index_lookup(self.ptr, to_slice(index_key), Some(cb), hasil_ptr)
        })?;
        Ok(hasil)
    }

    /// Cari semua primary key dalam rentang index [min_key, max_key].
    pub fn range(&self, min_key: &[u8], max_key: &[u8]) -> Result<Vec<Vec<u8>>> {
        let mut hasil: Vec<Vec<u8>> = Vec::new();
        let hasil_ptr = &mut hasil as *mut Vec<Vec<u8>> as *mut std::ffi::c_void;

        unsafe extern "C" fn cb(pk: ffi::fastkv_slice_t, udata: *mut std::ffi::c_void)
            -> ffi::fastkv_err_t
        {
            let hasil = &mut *(udata as *mut Vec<Vec<u8>>);
            if !pk.data.is_null() && pk.len > 0 {
                hasil.push(std::slice::from_raw_parts(pk.data, pk.len).to_vec());
            }
            ffi::fastkv_err_t::FASTKV_OK
        }

        error::check(unsafe {
            ffi::fastkv_index_range(
                self.ptr,
                to_slice(min_key),
                to_slice(max_key),
                Some(cb),
                hasil_ptr,
            )
        })?;
        Ok(hasil)
    }
}

impl Drop for Index<'_> {
    fn drop(&mut self) {
        // index tidak perlu di-destroy secara eksplisit — drop_index mengurus ini
        // kita biarkan db._ptr yang mengelola lifetime index
        let _ = &self.db;
        let _ = &self.name;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn open_db(path: &str) -> DB {
        fs::create_dir_all(path).unwrap();
        let mut opts = Opts::default();
        opts.sync_writes = false;
        DB::open(path, opts).expect("gagal buka DB")
    }

    fn cleanup(path: &str) {
        fs::remove_dir_all(path).ok();
    }

    #[test]
    fn test_put_get() {
        let path = "/tmp/fastkv_rust_test_put_get";
        cleanup(path);
        let db = open_db(path);
        db.put(b"k", b"v").unwrap();
        assert_eq!(db.get(b"k").unwrap(), b"v");
        cleanup(path);
    }

    #[test]
    fn test_not_found() {
        let path = "/tmp/fastkv_rust_test_not_found";
        cleanup(path);
        let db = open_db(path);
        assert_eq!(db.get(b"tidak_ada").unwrap_err(), Error::NotFound);
        cleanup(path);
    }

    #[test]
    fn test_delete() {
        let path = "/tmp/fastkv_rust_test_delete";
        cleanup(path);
        let db = open_db(path);
        db.put(b"x", b"y").unwrap();
        db.delete(b"x").unwrap();
        assert_eq!(db.get(b"x").unwrap_err(), Error::NotFound);
        cleanup(path);
    }

    #[test]
    fn test_txn_commit() {
        let path = "/tmp/fastkv_rust_test_txn";
        cleanup(path);
        let db = open_db(path);
        {
            let txn = db.begin(false).unwrap();
            txn.put(b"a", b"1").unwrap();
            txn.put(b"b", b"2").unwrap();
            txn.commit().unwrap();
        }
        assert_eq!(db.get(b"a").unwrap(), b"1");
        assert_eq!(db.get(b"b").unwrap(), b"2");
        cleanup(path);
    }

    #[test]
    fn test_txn_rollback() {
        let path = "/tmp/fastkv_rust_test_rollback";
        cleanup(path);
        let db = open_db(path);
        db.put(b"stabil", b"ya").unwrap();
        {
            let _txn = db.begin(false).unwrap();
            // txn dropped tanpa commit → rollback otomatis
        }
        assert_eq!(db.get(b"stabil").unwrap(), b"ya");
        cleanup(path);
    }

    #[test]
    fn test_cursor_forward() {
        let path = "/tmp/fastkv_rust_test_cursor";
        cleanup(path);
        let db = open_db(path);
        for i in 0..5u8 {
            db.put(format!("k{:02}", i).as_bytes(), b"v").unwrap();
        }
        let txn = db.begin(true).unwrap();
        let mut cur = txn.cursor(true).unwrap();
        let items: Vec<_> = cur.iter().collect();
        assert_eq!(items.len(), 5);
        // pastikan urutan naik
        let keys: Vec<Vec<u8>> = items.into_iter().map(|r| r.unwrap().0).collect();
        let mut sorted = keys.clone();
        sorted.sort();
        assert_eq!(keys, sorted);
        cleanup(path);
    }

    #[test]
    fn test_json_index() {
        let path = "/tmp/fastkv_rust_test_json_index";
        cleanup(path);
        let db = open_db(path);
        let idx = db.json_index("by_role", "role").unwrap();

        db.put(b"u:1", br#"{"role":"admin","name":"ali"}"#).unwrap();
        db.put(b"u:2", br#"{"role":"user","name":"budi"}"#).unwrap();
        db.put(b"u:3", br#"{"role":"admin","name":"citra"}"#).unwrap();

        let hasil = idx.lookup(b"admin").unwrap();
        assert_eq!(hasil.len(), 2);

        db.drop_index("by_role").unwrap();
        cleanup(path);
    }

    #[test]
    fn test_stats() {
        let path = "/tmp/fastkv_rust_test_stats";
        cleanup(path);
        let db = open_db(path);
        db.put(b"a", b"1").unwrap();
        db.put(b"b", b"2").unwrap();
        let s = db.stats().unwrap();
        assert!(s.num_keys >= 2, "num_keys: {}", s.num_keys);
        cleanup(path);
    }

    #[test]
    fn test_put_ttl() {
        let path = "/tmp/fastkv_rust_test_ttl";
        cleanup(path);
        let db = open_db(path);
        db.put_ttl(b"ttl", b"isi", 10_000).unwrap();
        assert_eq!(db.get(b"ttl").unwrap(), b"isi");
        cleanup(path);
    }
}
