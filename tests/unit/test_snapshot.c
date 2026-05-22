#include "fastkv.h"

#include "api/kv_api.h"
#include "persist/snapshot.h"
#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char  *tmp = "/tmp/fastkv_test_snapshot";
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

void test_snapshot_write_load_roundtrip(void) {
    /* Write some data */
    fastkv_put(db, FASTKV_STR("foo"), FASTKV_STR("bar"));
    fastkv_put(db, FASTKV_STR("abc"), FASTKV_STR("xyz"));
    fastkv_put(db, FASTKV_STR("num"), FASTKV_STR("42"));

    fastkv_ts_t snap_ts = fastkv_oracle_now(&db->txn_mgr.oracle);

    /* Write snapshot */
    fastkv_err_t rc = fastkv_snapshot_write(tmp, snap_ts, db);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc);

    /* Open a fresh db and load the snapshot */
    fastkv_close(db);
    db = NULL;

    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = tmp;
    opts.sync_writes   = false;
    rc                 = fastkv_open(&db, &opts);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc);

    /* Verify all keys are present */
    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("foo"), &val));
    TEST_ASSERT_EQUAL_MEMORY("bar", val.data, 3);

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("abc"), &val));
    TEST_ASSERT_EQUAL_MEMORY("xyz", val.data, 3);

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("num"), &val));
    TEST_ASSERT_EQUAL_MEMORY("42", val.data, 2);
}

void test_snapshot_empty_db(void) {
    fastkv_ts_t  ts = fastkv_oracle_now(&db->txn_mgr.oracle);
    fastkv_err_t rc = fastkv_snapshot_write(tmp, ts, db);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc);
}

void test_snapshot_deletes_not_in_snapshot(void) {
    /* Write then delete a key before snapshot */
    fastkv_put(db, FASTKV_STR("gone"), FASTKV_STR("data"));
    fastkv_delete(db, FASTKV_STR("gone"));
    fastkv_put(db, FASTKV_STR("alive"), FASTKV_STR("yes"));

    fastkv_ts_t ts = fastkv_oracle_now(&db->txn_mgr.oracle);
    fastkv_snapshot_write(tmp, ts, db);

    fastkv_close(db);
    db                 = NULL;
    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = tmp;
    opts.sync_writes   = false;
    fastkv_open(&db, &opts);

    /* Deleted key must not reappear */
    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_get(db, FASTKV_STR("gone"), &val));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("alive"), &val));
}

void test_checkpoint_via_sync(void) {
    fastkv_put(db, FASTKV_STR("ck"), FASTKV_STR("val"));
    fastkv_err_t rc = fastkv_sync(db);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc);

    /* A snapshot file should now exist */
    char cmd[256];
    snprintf(cmd, sizeof cmd, "ls %s/snapshot-*.bin 2>/dev/null | wc -l", tmp);
    FILE *p     = popen(cmd, "r");
    int   count = 0;
    fscanf(p, "%d", &count);
    pclose(p);
    TEST_ASSERT_GREATER_THAN(0, count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_snapshot_write_load_roundtrip);
    RUN_TEST(test_snapshot_empty_db);
    RUN_TEST(test_snapshot_deletes_not_in_snapshot);
    RUN_TEST(test_checkpoint_via_sync);
    return UNITY_END();
}
