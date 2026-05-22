#include "unity.h"
#include "fastkv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fastkv_db_t *db;

void setUp(void)
{
    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path        = "/tmp/fastkv_test_txn";
    opts.sync_writes = false;

    char cmd[256];
    snprintf(cmd, sizeof cmd, "mkdir -p %s", opts.path);
    system(cmd);

    fastkv_open(&db, &opts);
}

void tearDown(void)
{
    fastkv_close(db);
    system("rm -rf /tmp/fastkv_test_txn");
}

void test_txn_commit(void)
{
    fastkv_txn_t *txn;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_txn_begin(db, false, &txn));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_txn_put(txn, FASTKV_STR("a"), FASTKV_STR("1")));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_txn_commit(txn));

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("a"), &val));
}

void test_txn_abort_discards_writes(void)
{
    fastkv_txn_t *txn;
    fastkv_txn_begin(db, false, &txn);
    fastkv_txn_put(txn, FASTKV_STR("aborted_key"), FASTKV_STR("nope"));
    fastkv_txn_abort(txn);

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_get(db, FASTKV_STR("aborted_key"), &val));
}

void test_txn_read_your_own_writes(void)
{
    fastkv_txn_t *txn;
    fastkv_txn_begin(db, false, &txn);
    fastkv_txn_put(txn, FASTKV_STR("ry"), FASTKV_STR("own"));

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_txn_get(txn, FASTKV_STR("ry"), &val));
    TEST_ASSERT_EQUAL_MEMORY("own", val.data, 3);
    fastkv_txn_abort(txn);
}

void test_txn_ro_rejects_writes(void)
{
    fastkv_txn_t *txn;
    fastkv_txn_begin(db, true, &txn);
    fastkv_err_t rc = fastkv_txn_put(txn, FASTKV_STR("x"), FASTKV_STR("y"));
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_TXN_RO, rc);
    fastkv_txn_abort(txn);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_txn_commit);
    RUN_TEST(test_txn_abort_discards_writes);
    RUN_TEST(test_txn_read_your_own_writes);
    RUN_TEST(test_txn_ro_rejects_writes);
    return UNITY_END();
}
