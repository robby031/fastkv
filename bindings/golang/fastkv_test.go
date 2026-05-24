package fastkv

import (
	"fmt"
	"os"
	"sync"
	"testing"
	"time"
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

// waitUntil menunggu kondisi terpenuhi atau timeout.
func waitUntil(t *testing.T, timeout time.Duration, cond func() bool) bool {
	t.Helper()
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if cond() {
			return true
		}
		time.Sleep(100 * time.Millisecond)
	}
	return false
}

func TestReplServeConnect(t *testing.T) {
	pPath, rPath := dbPath(t), dbPath(t)
	defer os.RemoveAll(pPath)
	defer os.RemoveAll(rPath)

	opts := DefaultOpts()
	opts.SyncWrites = false

	primary, err := Open(pPath, opts)
	if err != nil {
		t.Fatal(err)
	}
	defer primary.Close()

	replica, err := Open(rPath, opts)
	if err != nil {
		t.Fatal(err)
	}
	defer replica.Close()

	if err := primary.Serve(17801); err != nil {
		t.Fatal("Serve:", err)
	}
	defer primary.StopServer()

	if err := replica.Connect("127.0.0.1", 17801); err != nil {
		t.Fatal("Connect:", err)
	}
	defer replica.Disconnect()

	ok := waitUntil(t, 5*time.Second, func() bool {
		return replica.PrimaryStat().Connected
	})
	if !ok {
		t.Fatal("replica tidak terhubung dalam 5 detik")
	}

	stat := replica.PrimaryStat()
	if !stat.Connected {
		t.Error("PrimaryStat.Connected harus true")
	}
}

func TestReplPutPropagates(t *testing.T) {
	pPath, rPath := dbPath(t), dbPath(t)
	defer os.RemoveAll(pPath)
	defer os.RemoveAll(rPath)

	opts := DefaultOpts()
	opts.SyncWrites = false

	primary, _ := Open(pPath, opts)
	defer primary.Close()
	replica, _ := Open(rPath, opts)
	defer replica.Close()

	primary.Serve(17802)
	defer primary.StopServer()
	replica.Connect("127.0.0.1", 17802)
	defer replica.Disconnect()

	waitUntil(t, 5*time.Second, func() bool {
		return replica.PrimaryStat().Connected
	})

	primary.Put([]byte("hello"), []byte("world"))
	primary.Put([]byte("foo"), []byte("bar"))

	ok := waitUntil(t, 5*time.Second, func() bool {
		v, err := replica.Get([]byte("hello"))
		return err == nil && string(v) == "world"
	})
	if !ok {
		t.Fatal("key 'hello' tidak muncul di replica")
	}

	v, err := replica.Get([]byte("foo"))
	if err != nil || string(v) != "bar" {
		t.Errorf("key 'foo': got %q %v", v, err)
	}
}

func TestReplDeletePropagates(t *testing.T) {
	pPath, rPath := dbPath(t), dbPath(t)
	defer os.RemoveAll(pPath)
	defer os.RemoveAll(rPath)

	opts := DefaultOpts()
	opts.SyncWrites = false

	primary, _ := Open(pPath, opts)
	defer primary.Close()
	replica, _ := Open(rPath, opts)
	defer replica.Close()

	primary.Serve(17803)
	defer primary.StopServer()
	replica.Connect("127.0.0.1", 17803)
	defer replica.Disconnect()

	waitUntil(t, 5*time.Second, func() bool {
		return replica.PrimaryStat().Connected
	})

	primary.Put([]byte("temp"), []byte("data"))

	waitUntil(t, 5*time.Second, func() bool {
		_, err := replica.Get([]byte("temp"))
		return err == nil
	})

	primary.Delete([]byte("temp"))

	ok := waitUntil(t, 5*time.Second, func() bool {
		_, err := replica.Get([]byte("temp"))
		return err == ErrNotFound
	})
	if !ok {
		t.Fatal("delete tidak terpropagasi ke replica")
	}
}

func TestReplPeers(t *testing.T) {
	pPath, rPath := dbPath(t), dbPath(t)
	defer os.RemoveAll(pPath)
	defer os.RemoveAll(rPath)

	opts := DefaultOpts()
	opts.SyncWrites = false

	primary, _ := Open(pPath, opts)
	defer primary.Close()
	replica, _ := Open(rPath, opts)
	defer replica.Close()

	primary.Serve(17804)
	defer primary.StopServer()
	replica.Connect("127.0.0.1", 17804)
	defer replica.Disconnect()

	waitUntil(t, 5*time.Second, func() bool {
		return replica.PrimaryStat().Connected
	})

	for i := 0; i < 100; i++ {
		primary.Put([]byte(fmt.Sprintf("k%d", i)), []byte(fmt.Sprintf("v%d", i)))
	}

	waitUntil(t, 5*time.Second, func() bool {
		v, err := replica.Get([]byte("k99"))
		return err == nil && string(v) == "v99"
	})

	peers, err := primary.Peers()
	if err != nil {
		t.Fatal(err)
	}
	if len(peers) != 1 {
		t.Fatalf("expected 1 peer, got %d", len(peers))
	}
	if !peers[0].Connected {
		t.Error("peer harus connected")
	}

	pstat := replica.PrimaryStat()
	if !pstat.Connected {
		t.Error("primary stat harus connected")
	}
	if pstat.BytesTotal == 0 {
		t.Error("BytesTotal harus > 0 setelah menerima data")
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
