#include "fastkv.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char  *tmp = "/tmp/fastkv_test_crash_recovery";
static fastkv_db_t *db;

void setUp(void) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", tmp, tmp);
    system(cmd);

    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = tmp;
    opts.sync_writes   = true;
    fastkv_open(&db, &opts);
}

void tearDown(void) {
    if (db) {
        fastkv_close(db);
        db = NULL;
    }
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", tmp);
    system(cmd);
}

static fastkv_db_t *reopen(void) {
    fastkv_close(db);
    db = NULL;

    fastkv_db_t  *newdb;
    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = tmp;
    opts.sync_writes   = true;
    fastkv_err_t rc    = fastkv_open(&newdb, &opts);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc);
    return newdb;
}

/* Basic WAL recovery: writes without explicit sync, reopen, data survives */
void test_wal_recovery_basic(void) {
    fastkv_put(db, FASTKV_STR("persist"), FASTKV_STR("yes"));
    fastkv_put(db, FASTKV_STR("key2"), FASTKV_STR("val2"));

    db = reopen();

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("persist"), &val));
    TEST_ASSERT_EQUAL_MEMORY("yes", val.data, 3);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("key2"), &val));
    TEST_ASSERT_EQUAL_MEMORY("val2", val.data, 4);
}

/* Deleted key must not reappear after recovery */
void test_wal_recovery_delete(void) {
    fastkv_put(db, FASTKV_STR("alive"), FASTKV_STR("yes"));
    fastkv_put(db, FASTKV_STR("gone"), FASTKV_STR("no"));
    fastkv_delete(db, FASTKV_STR("gone"));

    db = reopen();

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("alive"), &val));
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_get(db, FASTKV_STR("gone"), &val));
}

/* Checkpoint (fastkv_sync) + WAL recovery: data survives across reopen */
void test_checkpoint_then_recovery(void) {
    fastkv_put(db, FASTKV_STR("a"), FASTKV_STR("1"));
    fastkv_sync(db);                                  /* writes snapshot */
    fastkv_put(db, FASTKV_STR("b"), FASTKV_STR("2")); /* after checkpoint — in WAL only */

    db = reopen();

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("a"), &val));
    TEST_ASSERT_EQUAL_MEMORY("1", val.data, 1);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("b"), &val));
    TEST_ASSERT_EQUAL_MEMORY("2", val.data, 1);
}

/* Overwritten value after recovery reflects last write */
void test_overwrite_survives_recovery(void) {
    fastkv_put(db, FASTKV_STR("x"), FASTKV_STR("old"));
    fastkv_put(db, FASTKV_STR("x"), FASTKV_STR("new"));

    db = reopen();

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("x"), &val));
    TEST_ASSERT_EQUAL_MEMORY("new", val.data, 3);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_wal_recovery_basic);
    RUN_TEST(test_wal_recovery_delete);
    RUN_TEST(test_checkpoint_then_recovery);
    RUN_TEST(test_overwrite_survives_recovery);
    return UNITY_END();
}
