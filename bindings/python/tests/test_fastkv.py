"""
Test Python binding FastKV.
Run: pytest tests/
Make sure the library is built: python fastkv_ffi_build.py
"""

import os
import shutil
import tempfile

import pytest
import fastkv
from fastkv import DB, NotFoundError, ConflictError

DB_PATH = tempfile.mkdtemp(prefix="fastkv_pytest_")


@pytest.fixture(autouse=True)
def cleanup():
    shutil.rmtree(DB_PATH, ignore_errors=True)
    os.makedirs(DB_PATH)
    yield
    shutil.rmtree(DB_PATH, ignore_errors=True)
    os.makedirs(DB_PATH)


def test_put_get():
    with DB(DB_PATH, sync_writes=False) as db:
        db.put(b"key", b"value")
        assert db.get(b"key") == b"value"


def test_delete():
    with DB(DB_PATH, sync_writes=False) as db:
        db.put(b"x", b"y")
        db.delete(b"x")
        with pytest.raises(NotFoundError):
            db.get(b"x")


def test_not_found():
    with DB(DB_PATH, sync_writes=False) as db:
        with pytest.raises(NotFoundError):
            db.get(b"not_found")


def test_overwrite():
    with DB(DB_PATH, sync_writes=False) as db:
        db.put(b"k", b"old")
        db.put(b"k", b"new")
        assert db.get(b"k") == b"new"


def test_txn_commit():
    with DB(DB_PATH, sync_writes=False) as db:
        with db.begin() as txn:
            txn.put(b"a", b"1")
            txn.put(b"b", b"2")

        assert db.get(b"a") == b"1"
        assert db.get(b"b") == b"2"


def test_txn_abort():
    with DB(DB_PATH, sync_writes=False) as db:
        db.put(b"stable", b"value")

        txn = db.begin()
        txn.put(b"new", b"value")
        txn.abort()

        with pytest.raises(NotFoundError):
            db.get(b"new")
        assert db.get(b"stable") == b"value"


def test_txn_read_only():
    with DB(DB_PATH, sync_writes=False) as db:
        db.put(b"ro", b"value")
        with db.begin(read_only=True) as txn:
            assert txn.get(b"ro") == b"value"


def test_cursor_forward():
    with DB(DB_PATH, sync_writes=False) as db:
        for i in range(5):
            db.put(f"key{i:02d}".encode(), f"val{i}".encode())

        with db.begin(read_only=True) as txn:
            with txn.cursor() as cur:
                items = list(cur)

        assert len(items) == 5
        keys = [k for k, _ in items]
        assert keys == sorted(keys)


def test_cursor_backward():
    with DB(DB_PATH, sync_writes=False) as db:
        for i in range(5):
            db.put(f"key{i:02d}".encode(), f"val{i}".encode())

        with db.begin(read_only=True) as txn:
            with txn.cursor(forward=False) as cur:
                items = list(cur)

        keys = [k for k, _ in items]
        assert keys == sorted(keys, reverse=True)


def test_cursor_seek():
    with DB(DB_PATH, sync_writes=False) as db:
        for c in [b"a", b"c", b"e", b"g"]:
            db.put(c, b"x")

        with db.begin(read_only=True) as txn:
            with txn.cursor() as cur:
                cur.seek(b"c")
                keys = [cur.key()]
                while cur.next():
                    keys.append(cur.key())

        assert keys == [b"c", b"e", b"g"]


def test_json_index():
    with DB(DB_PATH, sync_writes=False) as db:
        idx = db.json_index("by_role", "role")

        db.put(b"u:1", b'{"role":"admin","name":"robby"}')
        db.put(b"u:2", b'{"role":"user","name":"eka"}')
        db.put(b"u:3", b'{"role":"admin","name":"arman"}')

        h = idx.lookup(b"admin")
        assert len(h) == 2
        assert b"u:1" in h
        assert b"u:3" in h

        db.drop_index("by_role")


def test_put_ttl_visible():
    with DB(DB_PATH, sync_writes=False) as db:
        db.put_ttl(b"temp", b"value", 10_000)
        assert db.get(b"temp") == b"value"


def test_stats():
    with DB(DB_PATH, sync_writes=False) as db:
        db.put(b"a", b"1")
        db.put(b"b", b"2")
        s = db.stats()
        assert s["num_keys"] >= 2
        assert s["num_txn_committed"] >= 2


def test_sync():
    with DB(DB_PATH, sync_writes=False) as db:
        db.put(b"flush", b"this")
        db.sync()
        assert db.get(b"flush") == b"this"
