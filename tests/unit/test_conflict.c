#include "fastkv.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char  *tmp = "/tmp/fastkv_test_conflict";
static fastkv_db_t *db;

void setUp(void) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", tmp, tmp);
    system(cmd);

    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = tmp;
    opts.sync_writes   = false;
    fastkv_open(&db, &opts);
}

void tearDown(void) {
    fastkv_close(db);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", tmp);
    system(cmd);
}

/* Two concurrent txns write the same key — second commit must conflict */
void test_ww_conflict_same_key(void) {
    fastkv_txn_t *t1, *t2;
    fastkv_txn_begin(db, false, &t1);
    fastkv_txn_begin(db, false, &t2);

    fastkv_txn_put(t1, FASTKV_STR("key"), FASTKV_STR("from-t1"));
    fastkv_txn_put(t2, FASTKV_STR("key"), FASTKV_STR("from-t2"));

    /* t1 commits first — must succeed */
    fastkv_err_t rc1 = fastkv_txn_commit(t1);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc1);

    /* t2 commits after — must conflict */
    fastkv_err_t rc2 = fastkv_txn_commit(t2);
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_TXN_CONFLICT, rc2);

    /* Winner's value must be visible */
    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("key"), &val));
    TEST_ASSERT_EQUAL_MEMORY("from-t1", val.data, 7);
}

/* Two txns write different keys — both should commit */
void test_no_conflict_different_keys(void) {
    fastkv_txn_t *t1, *t2;
    fastkv_txn_begin(db, false, &t1);
    fastkv_txn_begin(db, false, &t2);

    fastkv_txn_put(t1, FASTKV_STR("k1"), FASTKV_STR("v1"));
    fastkv_txn_put(t2, FASTKV_STR("k2"), FASTKV_STR("v2"));

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_txn_commit(t1));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_txn_commit(t2));

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("k1"), &val));
    TEST_ASSERT_EQUAL_MEMORY("v1", val.data, 2);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("k2"), &val));
    TEST_ASSERT_EQUAL_MEMORY("v2", val.data, 2);
}

/* Aborted txn must not be visible */
void test_abort_not_visible(void) {
    fastkv_txn_t *t;
    fastkv_txn_begin(db, false, &t);
    fastkv_txn_put(t, FASTKV_STR("ghost"), FASTKV_STR("value"));
    fastkv_txn_abort(t);

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_get(db, FASTKV_STR("ghost"), &val));
}

/* Read-only txn should see a consistent snapshot */
void test_readonly_snapshot_isolation(void) {
    fastkv_put(db, FASTKV_STR("snap"), FASTKV_STR("before"));

    fastkv_txn_t *ro;
    fastkv_txn_begin(db, true /* read-only */, &ro);

    /* Committed write after txn began */
    fastkv_put(db, FASTKV_STR("snap"), FASTKV_STR("after"));

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_txn_get(ro, FASTKV_STR("snap"), &val));
    TEST_ASSERT_EQUAL_MEMORY("before", val.data, 6);

    fastkv_txn_abort(ro);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ww_conflict_same_key);
    RUN_TEST(test_no_conflict_different_keys);
    RUN_TEST(test_abort_not_visible);
    RUN_TEST(test_readonly_snapshot_isolation);
    return UNITY_END();
}
