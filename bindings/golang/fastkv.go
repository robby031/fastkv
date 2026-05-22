// Package fastkv provides Go bindings for the FastKV library.
//
// Basic usage:
//
//	db, err := fastkv.Open("/tmp/mydb", fastkv.DefaultOpts())
//	if err != nil { log.Fatal(err) }
//	defer db.Close()
//
//	db.Put([]byte("key"), []byte("value"))
//	val, _ := db.Get([]byte("key"))
//	fmt.Println(string(val))
package fastkv

/*
#cgo CFLAGS: -I${SRCDIR}/../../include
#cgo LDFLAGS: -L${SRCDIR}/../../build/src -lfastkv -lpthread -lm

#include "fastkv.h"
#include <stdlib.h>

// declared in fastkv_cb_helper.c
fastkv_err_t fastkv_index_lookup_go(fastkv_index_t *idx, fastkv_slice_t ik, uintptr_t handle);
fastkv_err_t fastkv_index_range_go(fastkv_index_t *idx,
    fastkv_slice_t min_ik, fastkv_slice_t max_ik, uintptr_t handle);
*/
import "C"
import (
	"errors"
	"runtime"
	"runtime/cgo"
	"sync"
	"unsafe"
)

// Standard FastKV errors
var (
	ErrNotFound  = errors.New("key not found")
	ErrConflict  = errors.New("transaction write-write conflict")
	ErrReadOnly  = errors.New("write on read-only transaction")
	ErrCursorEOF = errors.New("cursor already exhausted")
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

// go_index_scan_cb is called from C (via wrap_scan_cb in helper.c).
// handle is a cgo.Handle wrapped as a uintptr_t — GC-safe.
//
//export go_index_scan_cb
func go_index_scan_cb(pk C.fastkv_slice_t, handle C.uintptr_t) C.int {
	h := cgo.Handle(handle)
	hasil := h.Value().(*[][]byte)
	*hasil = append(*hasil, fromSlice(pk))
	return C.int(C.FASTKV_OK)
}

// Opts contains configuration for opening the database.
type Opts struct {
	MapSize    int
	ArenaSize  int
	SyncWrites bool
	ReadOnly   bool
}

// DefaultOpts returns a reasonable default configuration.
func DefaultOpts() Opts {
	return Opts{
		MapSize:    1024 * 1024,
		ArenaSize:  64 * 1024,
		SyncWrites: true,
		ReadOnly:   false,
	}
}

// DB is the main handle for the FastKV database.
// Safe for concurrent use by multiple goroutines.
type DB struct {
	mu     sync.RWMutex
	ptr    *C.fastkv_db_t
	closed bool
}

// Open opens a database at the given path.
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

// Close closes the database.
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

// Sync performs an explicit WAL flush and checkpoint.
func (db *DB) Sync() error {
	db.mu.RLock()
	defer db.mu.RUnlock()
	return toError(C.fastkv_sync(db.ptr))
}

// Get reads the value for the given key.
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

// Put stores a key-value pair.
func (db *DB) Put(key, value []byte) error {
	db.mu.RLock()
	defer db.mu.RUnlock()
	return toError(C.fastkv_put(db.ptr, toSlice(key), toSlice(value)))
}

// PutTTL stores a key with a TTL in milliseconds.
func (db *DB) PutTTL(key, value []byte, ttlMs uint64) error {
	db.mu.RLock()
	defer db.mu.RUnlock()
	return toError(C.fastkv_put_ttl(db.ptr, toSlice(key), toSlice(value), C.uint64_t(ttlMs)))
}

// Delete removes a key from the database.
func (db *DB) Delete(key []byte) error {
	db.mu.RLock()
	defer db.mu.RUnlock()
	return toError(C.fastkv_delete(db.ptr, toSlice(key)))
}

// Stats returns operational statistics of the database.
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

// Stats contains operational statistics of the database.
type Stats struct {
	NumKeys         uint64
	NumTxnCommitted uint64
	NumTxnAborted   uint64
	NumTxnConflicts uint64
	WALBytesWritten uint64
	SnapshotCount   uint64
	ArenaAllocBytes uint64
}

// Begin starts a new transaction.
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

// JSONIndex creates a secondary index based on a JSON field.
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

// DropIndex removes the index with the given name.
func (db *DB) DropIndex(name string) error {
	db.mu.RLock()
	defer db.mu.RUnlock()
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))
	return toError(C.fastkv_index_drop(db.ptr, cname))
}

// Txn is an explicit FastKV transaction.
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

// Get reads a key within this transaction.
func (t *Txn) Get(key []byte) ([]byte, error) {
	var s C.fastkv_slice_t
	if err := toError(C.fastkv_txn_get(t.ptr, toSlice(key), &s)); err != nil {
		return nil, err
	}
	return fromSlice(s), nil
}

// Put stores a key-value pair within this transaction.
func (t *Txn) Put(key, value []byte) error {
	return toError(C.fastkv_txn_put(t.ptr, toSlice(key), toSlice(value)))
}

// Delete removes a key within this transaction.
func (t *Txn) Delete(key []byte) error {
	return toError(C.fastkv_txn_delete(t.ptr, toSlice(key)))
}

// Commit completes and commits the transaction.
func (t *Txn) Commit() error {
	if t.done {
		return errors.New("transaction already completed")
	}
	t.done = true
	runtime.SetFinalizer(t, nil)
	return toError(C.fastkv_txn_commit(t.ptr))
}

// Rollback aborts the transaction.
func (t *Txn) Rollback() {
	t.rollback()
	runtime.SetFinalizer(t, nil)
}

// Cursor opens a cursor on this transaction.
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

// Cursor is a sequential reader over a B+tree snapshot.
type Cursor struct {
	ptr    *C.fastkv_cursor_t
	closed bool
}

// Seek positions the cursor at the key >= key.
func (c *Cursor) Seek(key []byte) error {
	return toError(C.fastkv_cursor_seek(c.ptr, toSlice(key)))
}

// Next advances to the next entry. Returns (false, nil) if at the end.
func (c *Cursor) Next() (bool, error) {
	rc := C.fastkv_cursor_next(c.ptr)
	if rc == C.FASTKV_ERR_CURSOR_EOF {
		return false, nil
	}
	return rc == C.FASTKV_OK, toError(rc)
}

// Key returns the key at the current cursor position.
func (c *Cursor) Key() ([]byte, error) {
	var s C.fastkv_slice_t
	if err := toError(C.fastkv_cursor_key(c.ptr, &s)); err != nil {
		return nil, err
	}
	return fromSlice(s), nil
}

// Value returns the value at the current cursor position.
func (c *Cursor) Value() ([]byte, error) {
	var s C.fastkv_slice_t
	if err := toError(C.fastkv_cursor_value(c.ptr, &s)); err != nil {
		return nil, err
	}
	return fromSlice(s), nil
}

// Close closes the cursor.
func (c *Cursor) Close() {
	if !c.closed {
		C.fastkv_cursor_close(c.ptr)
		c.closed = true
		runtime.SetFinalizer(c, nil)
	}
}

// Index is a handle to a FastKV secondary index.
type Index struct {
	db   *DB
	ptr  *C.fastkv_index_t
	name string
}

// Lookup finds all primary keys matching the indexKey.
func (idx *Index) Lookup(indexKey []byte) ([][]byte, error) {
	var results [][]byte
	h := cgo.NewHandle(&results)
	defer h.Delete()

	rc := C.fastkv_index_lookup_go(idx.ptr, toSlice(indexKey), C.uintptr_t(h))
	if err := toError(rc); err != nil {
		return nil, err
	}
	return results, nil
}

// Range finds all primary keys within the index range [minKey, maxKey].
func (idx *Index) Range(minKey, maxKey []byte) ([][]byte, error) {
	var results [][]byte
	h := cgo.NewHandle(&results)
	defer h.Delete()

	rc := C.fastkv_index_range_go(idx.ptr, toSlice(minKey), toSlice(maxKey), C.uintptr_t(h))
	if err := toError(rc); err != nil {
		return nil, err
	}
	return results, nil
}

// SetLogLevel mengatur level log library fastkv.
type LogLevel int

const (
	LogTrace  LogLevel = 0
	LogDebug  LogLevel = 1
	LogInfo   LogLevel = 2
	LogWarn   LogLevel = 3
	LogError  LogLevel = 4
	LogFatal  LogLevel = 5
	LogSilent LogLevel = 6
)

// SetLogLevel sets the log level for the fastkv library.
func SetLogLevel(level LogLevel) {
	C.fastkv_set_log_level(C.int(level))
}
