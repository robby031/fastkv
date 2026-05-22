package fastkv

import (
	"fmt"
	"os"
	"sync"
	"testing"
)

func dbPath(t *testing.T) string {
	t.Helper()
	path, err := os.MkdirTemp("", "fastkv_go_test_*")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { os.RemoveAll(path) })
	return path
}

func openDB(t *testing.T) *DB {
	t.Helper()
	opts := DefaultOpts()
	opts.SyncWrites = false
	db, err := Open(dbPath(t), opts)
	if err != nil {
		t.Fatalf("Fail to open DB: %v", err)
	}
	t.Cleanup(func() { db.Close() })
	return db
}

func TestPutGet(t *testing.T) {
	db := openDB(t)

	if err := db.Put([]byte("key"), []byte("value")); err != nil {
		t.Fatal(err)
	}
	val, err := db.Get([]byte("key"))
	if err != nil {
		t.Fatal(err)
	}
	if string(val) != "value" {
		t.Errorf("wrong value: %q", val)
	}
}

func TestNotFound(t *testing.T) {
	db := openDB(t)
	_, err := db.Get([]byte("not_found"))
	if err != ErrNotFound {
		t.Errorf("should be ErrNotFound, got %v", err)
	}
}

func TestDelete(t *testing.T) {
	db := openDB(t)
	db.Put([]byte("del"), []byte("value"))
	if err := db.Delete([]byte("del")); err != nil {
		t.Fatal(err)
	}
	_, err := db.Get([]byte("del"))
	if err != ErrNotFound {
		t.Errorf("should be ErrNotFound after delete, got %v", err)
	}
}

func TestTxnCommit(t *testing.T) {
	db := openDB(t)

	txn, err := db.Begin(false)
	if err != nil {
		t.Fatal(err)
	}
	txn.Put([]byte("a"), []byte("1"))
	txn.Put([]byte("b"), []byte("2"))
	if err := txn.Commit(); err != nil {
		t.Fatal(err)
	}

	val, _ := db.Get([]byte("a"))
	if string(val) != "1" {
		t.Errorf("wrong value for a: %q", val)
	}
}

func TestTxnRollback(t *testing.T) {
	db := openDB(t)
	db.Put([]byte("stable"), []byte("value"))

	txn, _ := db.Begin(false)
	txn.Put([]byte("new"), []byte("value"))
	txn.Rollback()

	_, err := db.Get([]byte("new"))
	if err != ErrNotFound {
		t.Error("key should not exist after rollback")
	}
}

func TestCursorForward(t *testing.T) {
	db := openDB(t)
	for i := 0; i < 5; i++ {
		db.Put([]byte(fmt.Sprintf("k%02d", i)), []byte(fmt.Sprintf("v%d", i)))
	}

	txn, _ := db.Begin(true)
	defer txn.Rollback()

	cur, err := txn.Cursor(true)
	if err != nil {
		t.Fatal(err)
	}
	defer cur.Close()

	var keys []string
	for {
		k, err := cur.Key()
		if err != nil {
			break
		}
		keys = append(keys, string(k))
		ok, _ := cur.Next()
		if !ok {
			break
		}
	}

	if len(keys) != 5 {
		t.Errorf("number of keys: %d, expected 5", len(keys))
	}
	for i := 1; i < len(keys); i++ {
		if keys[i] <= keys[i-1] {
			t.Errorf("wrong order: %q after %q", keys[i], keys[i-1])
		}
	}
}

func TestCursorSeek(t *testing.T) {
	db := openDB(t)
	for _, k := range []string{"a", "c", "e", "g"} {
		db.Put([]byte(k), []byte("x"))
	}

	txn, _ := db.Begin(true)
	defer txn.Rollback()

	cur, _ := txn.Cursor(true)
	defer cur.Close()

	cur.Seek([]byte("c"))
	k, _ := cur.Key()
	if string(k) != "c" {
		t.Errorf("seek to c, got %q", k)
	}
	cur.Next()
	k, _ = cur.Key()
	if string(k) != "e" {
		t.Errorf("after c should be e, got %q", k)
	}
}

func TestJSONIndex(t *testing.T) {
	db := openDB(t)

	idx, err := db.JSONIndex("by_role", "role")
	if err != nil {
		t.Fatal(err)
	}
	defer db.DropIndex("by_role")

	db.Put([]byte("u:1"), []byte(`{"role":"admin","name":"ali"}`))
	db.Put([]byte("u:2"), []byte(`{"role":"user","name":"budi"}`))
	db.Put([]byte("u:3"), []byte(`{"role":"admin","name":"citra"}`))

	hasil, err := idx.Lookup([]byte("admin"))
	if err != nil {
		t.Fatal(err)
	}
	if len(hasil) != 2 {
		t.Errorf("should have 2 results, got %d", len(hasil))
	}
}

func TestConcurrentPut(t *testing.T) {
	db := openDB(t)
	var wg sync.WaitGroup
	for i := 0; i < 8; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				key := fmt.Sprintf("w%d:k%d", id, j)
				val := fmt.Sprintf("val_%d_%d", id, j)
				if err := db.Put([]byte(key), []byte(val)); err != nil {
					t.Errorf("Put failed: %v", err)
				}
			}
		}(i)
	}
	wg.Wait()

	stats, err := db.Stats()
	if err != nil {
		t.Fatal(err)
	}
	if stats.NumKeys < 800 {
		t.Errorf("number of keys %d, expected >= 800", stats.NumKeys)
	}
}

func TestPutTTL(t *testing.T) {
	db := openDB(t)
	if err := db.PutTTL([]byte("ttl"), []byte("value"), 10_000); err != nil {
		t.Fatal(err)
	}
	val, err := db.Get([]byte("ttl"))
	if err != nil {
		t.Fatal(err)
	}
	if string(val) != "value" {
		t.Errorf("wrong TTL value: %q", val)
	}
}

func BenchmarkPut(b *testing.B) {
	opts := DefaultOpts()
	opts.SyncWrites = false
	path, _ := os.MkdirTemp("", "fastkv_bench_*")
	defer os.RemoveAll(path)
	db, _ := Open(path, opts)
	defer db.Close()

	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		key := fmt.Sprintf("key%d", i)
		db.Put([]byte(key), []byte("value"))
	}
}

func BenchmarkGet(b *testing.B) {
	opts := DefaultOpts()
	opts.SyncWrites = false
	path, _ := os.MkdirTemp("", "fastkv_bench_*")
	defer os.RemoveAll(path)
	db, _ := Open(path, opts)
	defer db.Close()

	db.Put([]byte("bench_key"), []byte("bench_val"))
	b.ResetTimer()
	b.ReportAllocs()
	for i := 0; i < b.N; i++ {
		db.Get([]byte("bench_key"))
	}
}
