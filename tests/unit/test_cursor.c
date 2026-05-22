#include "fastkv.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char  *tmp = "/tmp/fastkv_test_cursor";
static fastkv_db_t *db;

void setUp(void) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", tmp, tmp);
    system(cmd);

    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = tmp;
    opts.sync_writes   = false;
    fastkv_open(&db, &opts);

    /* isi beberapa kunci dalam urutan tidak teratur */
    fastkv_put(db, FASTKV_STR("dog"), FASTKV_STR("hewan"));
    fastkv_put(db, FASTKV_STR("apple"), FASTKV_STR("buah"));
    fastkv_put(db, FASTKV_STR("cat"), FASTKV_STR("hewan"));
    fastkv_put(db, FASTKV_STR("banana"), FASTKV_STR("buah"));
    fastkv_put(db, FASTKV_STR("elephant"), FASTKV_STR("hewan"));
}

void tearDown(void) {
    fastkv_close(db);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", tmp);
    system(cmd);
}

void test_cursor_forward_sorted(void) {
    fastkv_txn_t *txn;
    fastkv_txn_begin(db, true, &txn);

    fastkv_cursor_t *c;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_cursor_open(txn, FASTKV_CURSOR_FORWARD, &c));

    fastkv_slice_t key;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_cursor_key(c, &key));
    TEST_ASSERT_EQUAL_MEMORY("apple", key.data, 5);

    fastkv_cursor_next(c);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_cursor_key(c, &key));
    TEST_ASSERT_EQUAL_MEMORY("banana", key.data, 6);

    fastkv_cursor_next(c);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_cursor_key(c, &key));
    TEST_ASSERT_EQUAL_MEMORY("cat", key.data, 3);

    fastkv_cursor_close(c);
    fastkv_txn_abort(txn);
}

void test_cursor_backward_sorted(void) {
    fastkv_txn_t *txn;
    fastkv_txn_begin(db, true, &txn);

    fastkv_cursor_t *c;
    fastkv_cursor_open(txn, FASTKV_CURSOR_BACKWARD, &c);

    fastkv_slice_t key;
    fastkv_cursor_key(c, &key);
    TEST_ASSERT_EQUAL_MEMORY("elephant", key.data, 8);

    fastkv_cursor_next(c);
    fastkv_cursor_key(c, &key);
    TEST_ASSERT_EQUAL_MEMORY("dog", key.data, 3);

    fastkv_cursor_close(c);
    fastkv_txn_abort(txn);
}

void test_cursor_seek(void) {
    fastkv_txn_t *txn;
    fastkv_txn_begin(db, true, &txn);

    fastkv_cursor_t *c;
    fastkv_cursor_open(txn, FASTKV_CURSOR_FORWARD, &c);

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_cursor_seek(c, FASTKV_STR("cat")));

    fastkv_slice_t key;
    fastkv_cursor_key(c, &key);
    TEST_ASSERT_EQUAL_MEMORY("cat", key.data, 3);

    /* next dari cat harus dog */
    fastkv_cursor_next(c);
    fastkv_cursor_key(c, &key);
    TEST_ASSERT_EQUAL_MEMORY("dog", key.data, 3);

    fastkv_cursor_close(c);
    fastkv_txn_abort(txn);
}

void test_cursor_eof(void) {
    fastkv_txn_t *txn;
    fastkv_txn_begin(db, true, &txn);

    fastkv_cursor_t *c;
    fastkv_cursor_open(txn, FASTKV_CURSOR_FORWARD, &c);

    /* lewati semua entri */
    while (fastkv_cursor_next(c) == FASTKV_OK) {
    }

    fastkv_slice_t key;
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_CURSOR_EOF, fastkv_cursor_key(c, &key));

    fastkv_cursor_close(c);
    fastkv_txn_abort(txn);
}

void test_cursor_deleted_key_not_visible(void) {
    fastkv_delete(db, FASTKV_STR("cat"));

    fastkv_txn_t *txn;
    fastkv_txn_begin(db, true, &txn);

    fastkv_cursor_t *c;
    fastkv_cursor_open(txn, FASTKV_CURSOR_FORWARD, &c);

    /* kumpulkan semua kunci */
    char           found_cat = 0;
    fastkv_slice_t key;
    while (fastkv_cursor_key(c, &key) == FASTKV_OK) {
        if (key.len == 3 && memcmp(key.data, "cat", 3) == 0)
            found_cat = 1;
        fastkv_cursor_next(c);
    }

    TEST_ASSERT_EQUAL_INT(0, found_cat);

    fastkv_cursor_close(c);
    fastkv_txn_abort(txn);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cursor_forward_sorted);
    RUN_TEST(test_cursor_backward_sorted);
    RUN_TEST(test_cursor_seek);
    RUN_TEST(test_cursor_eof);
    RUN_TEST(test_cursor_deleted_key_not_visible);
    return UNITY_END();
}
