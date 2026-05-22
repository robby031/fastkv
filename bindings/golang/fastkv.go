// Package fastkv menyediakan binding Go untuk library FastKV.
//
// Penggunaan dasar:
//
//	db, err := fastkv.Open("/tmp/mydb", fastkv.DefaultOpts())
//	if err != nil { log.Fatal(err) }
//	defer db.Close()
//
//	db.Put([]byte("kunci"), []byte("nilai"))
//	val, _ := db.Get([]byte("kunci"))
//	fmt.Println(string(val))
package fastkv

/*
#cgo CFLAGS: -I${SRCDIR}/../../include
#cgo LDFLAGS: -L${SRCDIR}/../../build/src -lfastkv -lpthread -lm

#include "fastkv.h"
#include <stdlib.h>

// dideklarasikan di fastkv_cb_helper.c
fastkv_err_t fastkv_index_lookup_go(fastkv_index_t *idx, fastkv_slice_t ik, void *handle);
fastkv_err_t fastkv_index_range_go(fastkv_index_t *idx,
    fastkv_slice_t min_ik, fastkv_slice_t max_ik, void *handle);
*/
import "C"
import (
	"errors"
	"runtime"
	"runtime/cgo"
	"sync"
	"unsafe"
)

// Error standar FastKV
var (
	ErrNotFound  = errors.New("kunci tidak ditemukan")
	ErrConflict  = errors.New("konflik transaksi write-write")
	ErrReadOnly  = errors.New("write pada transaksi read-only")
	ErrCursorEOF = errors.New("cursor sudah habis")
)

func toError(rc C.fastkv_err_t) error {
	if rc == C.FASTKV_OK {
		return nil
	}
	switch rc {
	case C.FASTKV_ERR_NOTFOUND:
		return ErrNotFound
	case C.FASTKV_ERR_TXN_CONFLICT:
		return ErrConflict
	case C.FASTKV_ERR_TXN_RO:
		return ErrReadOnly
	case C.FASTKV_ERR_CURSOR_EOF:
		return ErrCursorEOF
	}
	return errors.New(C.GoString(C.fastkv_strerror(rc)))
}

func toSlice(b []byte) C.fastkv_slice_t {
	var s C.fastkv_slice_t
	if len(b) == 0 {
		return s
	}
	s.data = (*C.uint8_t)(unsafe.Pointer(&b[0]))
	s.len = C.size_t(len(b))
	return s
}

func fromSlice(s C.fastkv_slice_t) []byte {
	if s.data == nil || s.len == 0 {
		return nil
	}
	return C.GoBytes(unsafe.Pointer(s.data), C.int(s.len))
}

// go_index_scan_cb dipanggil dari C (via wrap_scan_cb di helper.c).
// udata adalah cgo.Handle yang membungkus *[][]byte.
//
//export go_index_scan_cb
func go_index_scan_cb(pk C.fastkv_slice_t, udata unsafe.Pointer) C.int {
	h := cgo.Handle(udata)
	hasil := h.Value().(*[][]byte)
	*hasil = append(*hasil, fromSlice(pk))
	return C.int(C.FASTKV_OK)
}

// Opts berisi konfigurasi pembukaan database.
type Opts struct {
	MapSize    int
	ArenaSize  int
	SyncWrites bool
	ReadOnly   bool
}

// DefaultOpts mengembalikan konfigurasi default yang wajar.
func DefaultOpts() Opts {
	return Opts{
		MapSize:    1024 * 1024,
		ArenaSize:  64 * 1024,
		SyncWrites: true,
		ReadOnly:   false,
	}
}

// DB adalah handle utama database FastKV.
// Aman digunakan dari banyak goroutine secara bersamaan.
type DB struct {
	mu     sync.RWMutex
	ptr    *C.fastkv_db_t
	closed bool
}

// Open membuka database di path yang diberikan.
func Open(path string, opts Opts) (*DB, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))

	var copts C.fastkv_opts_t
	copts.path = cpath
	copts.map_size = C.size_t(opts.MapSize)
	copts.arena_size = C.size_t(opts.ArenaSize)
	copts.sync_writes = C.bool(opts.SyncWrites)
	copts.read_only = C.bool(opts.ReadOnly)

	var ptr *C.fastkv_db_t
	if err := toError(C.fastkv_open(&ptr, &copts)); err != nil {
		return nil, err
	}

	db := &DB{ptr: ptr}
	runtime.SetFinalizer(db, (*DB).Close)
	return db, nil
}

// Close menutup database.
func (db *DB) Close() error {
	db.mu.Lock()
	defer db.mu.Unlock()
	if db.closed {
		return nil
	}
	db.closed = true
	runtime.SetFinalizer(db, nil)
	return toError(C.fastkv_close(db.ptr))
}

// Sync melakukan WAL flush dan checkpoint secara eksplisit.
func (db *DB) Sync() error {
	db.mu.RLock()
	defer db.mu.RUnlock()
	return toError(C.fastkv_sync(db.ptr))
}

// Get membaca nilai untuk kunci yang diberikan.
func (db *DB) Get(key []byte) ([]byte, error) {
	db.mu.RLock()
	defer db.mu.RUnlock()
	var s C.fastkv_slice_t
	if err := toError(C.fastkv_get(db.ptr, toSlice(key), &s)); err != nil {
		return nil, err
	}
	val := fromSlice(s)
	C.fastkv_free_value(db.ptr, &s)
	return val, nil
}

// Put menyimpan pasangan kunci-nilai.
func (db *DB) Put(key, value []byte) error {
	db.mu.RLock()
	defer db.mu.RUnlock()
	return toError(C.fastkv_put(db.ptr, toSlice(key), toSlice(value)))
}

// PutTTL menyimpan kunci dengan TTL dalam milidetik.
func (db *DB) PutTTL(key, value []byte, ttlMs uint64) error {
	db.mu.RLock()
	defer db.mu.RUnlock()
	return toError(C.fastkv_put_ttl(db.ptr, toSlice(key), toSlice(value), C.uint64_t(ttlMs)))
}

// Delete menghapus kunci dari database.
func (db *DB) Delete(key []byte) error {
	db.mu.RLock()
	defer db.mu.RUnlock()
	return toError(C.fastkv_delete(db.ptr, toSlice(key)))
}

// Stats mengembalikan statistik operasional database.
func (db *DB) Stats() (Stats, error) {
	db.mu.RLock()
	defer db.mu.RUnlock()
	var cs C.fastkv_stats_t
	if err := toError(C.fastkv_stats(db.ptr, &cs)); err != nil {
		return Stats{}, err
	}
	return Stats{
		NumKeys:         uint64(cs.num_keys),
		NumTxnCommitted: uint64(cs.num_txn_committed),
		NumTxnAborted:   uint64(cs.num_txn_aborted),
		NumTxnConflicts: uint64(cs.num_txn_conflicts),
		WALBytesWritten: uint64(cs.wal_bytes_written),
		SnapshotCount:   uint64(cs.snapshot_count),
		ArenaAllocBytes: uint64(cs.arena_alloc_bytes),
	}, nil
}

// Stats berisi statistik operasional database.
type Stats struct {
	NumKeys         uint64
	NumTxnCommitted uint64
	NumTxnAborted   uint64
	NumTxnConflicts uint64
	WALBytesWritten uint64
	SnapshotCount   uint64
	ArenaAllocBytes uint64
}

// Begin membuka transaksi baru.
func (db *DB) Begin(readOnly bool) (*Txn, error) {
	db.mu.RLock()
	var ptr *C.fastkv_txn_t
	rc := C.fastkv_txn_begin(db.ptr, C.bool(readOnly), &ptr)
	db.mu.RUnlock()
	if err := toError(rc); err != nil {
		return nil, err
	}
	t := &Txn{db: db, ptr: ptr}
	runtime.SetFinalizer(t, (*Txn).rollback)
	return t, nil
}

// JSONIndex membuat index sekunder berbasis field JSON.
func (db *DB) JSONIndex(name, field string) (*Index, error) {
	db.mu.RLock()
	defer db.mu.RUnlock()

	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))
	cfield := C.CString(field)
	defer C.free(unsafe.Pointer(cfield))

	var ptr *C.fastkv_index_t
	if err := toError(C.fastkv_json_index_create(db.ptr, cname, cfield, &ptr)); err != nil {
		return nil, err
	}
	return &Index{db: db, ptr: ptr, name: name}, nil
}

// DropIndex menghapus index dengan nama yang diberikan.
func (db *DB) DropIndex(name string) error {
	db.mu.RLock()
	defer db.mu.RUnlock()
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))
	return toError(C.fastkv_index_drop(db.ptr, cname))
}

// Txn adalah transaksi eksplisit FastKV.
type Txn struct {
	db   *DB
	ptr  *C.fastkv_txn_t
	done bool
}

func (t *Txn) rollback() {
	if !t.done {
		C.fastkv_txn_abort(t.ptr)
		t.done = true
	}
}

// Get membaca kunci dalam transaksi ini.
func (t *Txn) Get(key []byte) ([]byte, error) {
	var s C.fastkv_slice_t
	if err := toError(C.fastkv_txn_get(t.ptr, toSlice(key), &s)); err != nil {
		return nil, err
	}
	return fromSlice(s), nil
}

// Put menyimpan kunci-nilai dalam transaksi ini.
func (t *Txn) Put(key, value []byte) error {
	return toError(C.fastkv_txn_put(t.ptr, toSlice(key), toSlice(value)))
}

// Delete menghapus kunci dalam transaksi ini.
func (t *Txn) Delete(key []byte) error {
	return toError(C.fastkv_txn_delete(t.ptr, toSlice(key)))
}

// Commit menyelesaikan dan mengkomit transaksi.
func (t *Txn) Commit() error {
	if t.done {
		return errors.New("transaksi sudah selesai")
	}
	t.done = true
	runtime.SetFinalizer(t, nil)
	return toError(C.fastkv_txn_commit(t.ptr))
}

// Rollback membatalkan transaksi.
func (t *Txn) Rollback() {
	t.rollback()
	runtime.SetFinalizer(t, nil)
}

// Cursor membuka cursor pada transaksi ini.
func (t *Txn) Cursor(forward bool) (*Cursor, error) {
	dir := C.FASTKV_CURSOR_FORWARD
	if !forward {
		dir = C.FASTKV_CURSOR_BACKWARD
	}
	var ptr *C.fastkv_cursor_t
	if err := toError(C.fastkv_cursor_open(t.ptr, C.fastkv_cursor_dir_t(dir), &ptr)); err != nil {
		return nil, err
	}
	c := &Cursor{ptr: ptr}
	runtime.SetFinalizer(c, (*Cursor).Close)
	return c, nil
}

// Cursor adalah pembaca berurutan atas snapshot B+tree.
type Cursor struct {
	ptr    *C.fastkv_cursor_t
	closed bool
}

// Seek memposisikan cursor ke kunci >= key.
func (c *Cursor) Seek(key []byte) error {
	return toError(C.fastkv_cursor_seek(c.ptr, toSlice(key)))
}

// Next maju ke entri berikutnya. Kembalikan (false, nil) jika sudah habis.
func (c *Cursor) Next() (bool, error) {
	rc := C.fastkv_cursor_next(c.ptr)
	if rc == C.FASTKV_ERR_CURSOR_EOF {
		return false, nil
	}
	return rc == C.FASTKV_OK, toError(rc)
}

// Key mengembalikan kunci pada posisi cursor saat ini.
func (c *Cursor) Key() ([]byte, error) {
	var s C.fastkv_slice_t
	if err := toError(C.fastkv_cursor_key(c.ptr, &s)); err != nil {
		return nil, err
	}
	return fromSlice(s), nil
}

// Value mengembalikan nilai pada posisi cursor saat ini.
func (c *Cursor) Value() ([]byte, error) {
	var s C.fastkv_slice_t
	if err := toError(C.fastkv_cursor_value(c.ptr, &s)); err != nil {
		return nil, err
	}
	return fromSlice(s), nil
}

// Close menutup cursor.
func (c *Cursor) Close() {
	if !c.closed {
		C.fastkv_cursor_close(c.ptr)
		c.closed = true
		runtime.SetFinalizer(c, nil)
	}
}

// Index adalah handle index sekunder FastKV.
type Index struct {
	db   *DB
	ptr  *C.fastkv_index_t
	name string
}

// Lookup mencari semua primary key yang cocok dengan indexKey.
func (idx *Index) Lookup(indexKey []byte) ([][]byte, error) {
	var hasil [][]byte
	h := cgo.NewHandle(&hasil)
	defer h.Delete()

	rc := C.fastkv_index_lookup_go(idx.ptr, toSlice(indexKey), unsafe.Pointer(h))
	if err := toError(rc); err != nil {
		return nil, err
	}
	return hasil, nil
}

// Range mencari semua primary key dalam rentang index [minKey, maxKey].
func (idx *Index) Range(minKey, maxKey []byte) ([][]byte, error) {
	var hasil [][]byte
	h := cgo.NewHandle(&hasil)
	defer h.Delete()

	rc := C.fastkv_index_range_go(idx.ptr, toSlice(minKey), toSlice(maxKey), unsafe.Pointer(h))
	if err := toError(rc); err != nil {
		return nil, err
	}
	return hasil, nil
}
