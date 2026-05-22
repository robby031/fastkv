"""
FastKV Python binding — antarmuka high-level via CFFI.

Contoh penggunaan:

    import fastkv

    with fastkv.DB("/tmp/mydb") as db:
        db.put(b"kunci", b"nilai")
        print(db.get(b"kunci"))   # b"nilai"
        db.delete(b"kunci")

    # transaksi eksplisit
    with fastkv.DB("/tmp/mydb") as db:
        with db.begin() as txn:
            txn.put(b"a", b"1")
            txn.put(b"b", b"2")
            # commit otomatis saat keluar blok with

    # scan range via cursor
    with fastkv.DB("/tmp/mydb") as db:
        with db.begin(read_only=True) as txn:
            with txn.cursor() as cur:
                cur.seek(b"a")
                while True:
                    k, v = cur.item()
                    print(k, v)
                    if not cur.next():
                        break
"""

from ._libfastkv import ffi, lib
from typing import Iterator, Optional, Tuple

__all__ = [
    "DB",
    "Transaction",
    "Cursor",
    "Index",
    "FastKVError",
    "NotFoundError",
    "ConflictError",
]


def _check(rc):
    """Lempar exception jika rc != FASTKV_OK."""
    if rc == lib.FASTKV_OK:
        return
    msg = ffi.string(lib.fastkv_strerror(rc)).decode()
    if rc == lib.FASTKV_ERR_NOTFOUND:
        raise NotFoundError(msg)
    if rc == lib.FASTKV_ERR_TXN_CONFLICT:
        raise ConflictError(msg)
    raise FastKVError(rc, msg)


def _slice(data: bytes) -> "ffi CData":
    """Buat fastkv_slice_t dari bytes Python."""
    s = ffi.new("fastkv_slice_t *")
    s.data = ffi.cast("const uint8_t *", ffi.from_buffer(data))
    s.len = len(data)
    return s[0]


def _slice_to_bytes(s) -> bytes:
    """Salin fastkv_slice_t ke bytes Python."""
    return bytes(ffi.buffer(s.data, s.len))


class FastKVError(Exception):
    def __init__(self, code: int, msg: str):
        super().__init__(msg)
        self.code = code


class NotFoundError(FastKVError):
    def __init__(self, msg="kunci tidak ditemukan"):
        super().__init__(lib.FASTKV_ERR_NOTFOUND, msg)


class ConflictError(FastKVError):
    def __init__(self, msg="konflik transaksi"):
        super().__init__(lib.FASTKV_ERR_TXN_CONFLICT, msg)


class Cursor:
    """Pembaca berurutan atas snapshot B+tree dalam satu transaksi."""

    def __init__(self, txn: "Transaction", forward: bool = True):
        ptr = ffi.new("fastkv_cursor_t **")
        direction = lib.FASTKV_CURSOR_FORWARD if forward else lib.FASTKV_CURSOR_BACKWARD
        _check(lib.fastkv_cursor_open(txn._ptr, direction, ptr))
        self._ptr = ptr[0]
        self._closed = False

    def seek(self, key: bytes) -> "Cursor":
        """Posisikan cursor ke kunci pertama >= key."""
        _check(lib.fastkv_cursor_seek(self._ptr, _slice(key)))
        return self

    def next(self) -> bool:
        """Maju ke entri berikutnya. Kembalikan False jika sudah habis."""
        rc = lib.fastkv_cursor_next(self._ptr)
        if rc == lib.FASTKV_ERR_CURSOR_EOF:
            return False
        _check(rc)
        return True

    def key(self) -> bytes:
        """Kembalikan kunci pada posisi cursor saat ini."""
        s = ffi.new("fastkv_slice_t *")
        _check(lib.fastkv_cursor_key(self._ptr, s))
        return _slice_to_bytes(s[0])

    def value(self) -> bytes:
        """Kembalikan nilai pada posisi cursor saat ini."""
        s = ffi.new("fastkv_slice_t *")
        _check(lib.fastkv_cursor_value(self._ptr, s))
        return _slice_to_bytes(s[0])

    def item(self) -> Tuple[bytes, bytes]:
        """Kembalikan (kunci, nilai) sekaligus."""
        return self.key(), self.value()

    def __iter__(self) -> Iterator[Tuple[bytes, bytes]]:
        """Iterasi seluruh entri dari posisi cursor saat ini."""
        while True:
            try:
                yield self.item()
            except FastKVError:
                break
            if not self.next():
                break

    def close(self):
        if not self._closed and self._ptr:
            lib.fastkv_cursor_close(self._ptr)
            self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        self.close()


class Transaction:
    """Transaksi eksplisit FastKV."""

    def __init__(self, db: "DB", read_only: bool = False):
        self._db = db
        self._read_only = read_only
        ptr = ffi.new("fastkv_txn_t **")
        _check(lib.fastkv_txn_begin(db._ptr, read_only, ptr))
        self._ptr = ptr[0]
        self._done = False

    def get(self, key: bytes) -> bytes:
        s = ffi.new("fastkv_slice_t *")
        _check(lib.fastkv_txn_get(self._ptr, _slice(key), s))
        return _slice_to_bytes(s[0])

    def put(self, key: bytes, value: bytes):
        _check(lib.fastkv_txn_put(self._ptr, _slice(key), _slice(value)))

    def delete(self, key: bytes):
        _check(lib.fastkv_txn_delete(self._ptr, _slice(key)))

    def commit(self):
        if self._done:
            raise FastKVError(lib.FASTKV_ERR_TXN_CLOSED, "transaksi sudah selesai")
        _check(lib.fastkv_txn_commit(self._ptr))
        self._done = True

    def abort(self):
        if self._done:
            return
        lib.fastkv_txn_abort(self._ptr)
        self._done = True

    def cursor(self, forward: bool = True) -> Cursor:
        return Cursor(self, forward=forward)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            try:
                self.commit()
            except Exception:
                self.abort()
                raise
        else:
            self.abort()
        return False

    def __del__(self):
        if not self._done:
            self.abort()


class Index:
    """Index sekunder FastKV."""

    def __init__(self, ptr):
        self._ptr = ptr

    def lookup(self, index_key: bytes) -> list[bytes]:
        """Cari semua primary key yang cocok dengan index_key."""
        hasil = []

        @ffi.callback("fastkv_err_t(fastkv_slice_t, void *)")
        def cb(pk, _udata):
            hasil.append(_slice_to_bytes(pk))
            return lib.FASTKV_OK

        _check(lib.fastkv_index_lookup(self._ptr, _slice(index_key), cb, ffi.NULL))
        return hasil

    def range(self, min_key: bytes, max_key: bytes) -> list[bytes]:
        """Cari semua primary key dalam rentang index [min_key, max_key]."""
        hasil = []

        @ffi.callback("fastkv_err_t(fastkv_slice_t, void *)")
        def cb(pk, _udata):
            hasil.append(_slice_to_bytes(pk))
            return lib.FASTKV_OK

        _check(lib.fastkv_index_range(
            self._ptr, _slice(min_key), _slice(max_key), cb, ffi.NULL))
        return hasil


class DB:
    """Handle utama database FastKV."""

    def __init__(
        self,
        path: str,
        map_size: int = 1024 * 1024,
        arena_size: int = 64 * 1024,
        sync_writes: bool = True,
        read_only: bool = False,
    ):
        # set dulu sebelum fastkv_open, supaya __del__ aman jika __init__ gagal
        self._closed = True
        self._ptr = ffi.NULL

        # _path_buf harus tetap hidup selama DB hidup — db->opts.path menunjuk ke sini
        self._path_buf = ffi.new("char[]", path.encode())
        opts = ffi.new("fastkv_opts_t *")
        opts.path = self._path_buf
        opts.map_size = map_size
        opts.arena_size = arena_size
        opts.sync_writes = sync_writes
        opts.read_only = read_only
        opts.malloc_fn = ffi.NULL
        opts.free_fn = ffi.NULL

        ptr = ffi.new("fastkv_db_t **")
        _check(lib.fastkv_open(ptr, opts))
        self._ptr = ptr[0]
        self._closed = False
        self._opts_ref = opts  # jaga agar tidak di-GC

    def get(self, key: bytes) -> bytes:
        s = ffi.new("fastkv_slice_t *")
        _check(lib.fastkv_get(self._ptr, _slice(key), s))
        val = _slice_to_bytes(s[0])
        lib.fastkv_free_value(self._ptr, s)
        return val

    def put(self, key: bytes, value: bytes):
        _check(lib.fastkv_put(self._ptr, _slice(key), _slice(value)))

    def put_ttl(self, key: bytes, value: bytes, ttl_ms: int):
        """Simpan kunci dengan TTL — otomatis dihapus setelah ttl_ms milidetik."""
        _check(lib.fastkv_put_ttl(self._ptr, _slice(key), _slice(value), ttl_ms))

    def delete(self, key: bytes):
        _check(lib.fastkv_delete(self._ptr, _slice(key)))

    def sync(self):
        """Flush WAL dan jalankan checkpoint."""
        _check(lib.fastkv_sync(self._ptr))

    def stats(self) -> dict:
        """Kembalikan statistik database sebagai dict."""
        s = ffi.new("fastkv_stats_t *")
        _check(lib.fastkv_stats(self._ptr, s))
        return {
            "num_keys": int(s.num_keys),
            "num_txn_committed": int(s.num_txn_committed),
            "num_txn_aborted": int(s.num_txn_aborted),
            "num_txn_conflicts": int(s.num_txn_conflicts),
            "wal_bytes_written": int(s.wal_bytes_written),
            "snapshot_count": int(s.snapshot_count),
            "arena_alloc_bytes": int(s.arena_alloc_bytes),
        }

    def begin(self, read_only: bool = False) -> Transaction:
        return Transaction(self, read_only=read_only)

    def json_index(self, name: str, field: str) -> Index:
        """Buat index berbasis field JSON."""
        ptr = ffi.new("fastkv_index_t **")
        _check(lib.fastkv_json_index_create(
            self._ptr,
            ffi.new("char[]", name.encode()),
            ffi.new("char[]", field.encode()),
            ptr,
        ))
        return Index(ptr[0])

    def drop_index(self, name: str):
        _check(lib.fastkv_index_drop(
            self._ptr, ffi.new("char[]", name.encode())))

    def close(self):
        if not self._closed and self._ptr != ffi.NULL:
            lib.fastkv_close(self._ptr)
            self._closed = True

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
        return False

    def __del__(self):
        self.close()
