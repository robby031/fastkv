#include "unity.h"
#include "fastkv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fastkv_db_t  *db;
static const char   *db_path = "/tmp/fastkv_test_crud";

void setUp(void)
{
    char cmd[256];
    snprintf(cmd, sizeof cmd, "mkdir -p %s", db_path);
    system(cmd);

    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path        = db_path;
    opts.sync_writes = false;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_open(&db, &opts));
}

void tearDown(void)
{
    fastkv_close(db);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", db_path);
    system(cmd);
}

void test_put_get(void)
{
    TEST_ASSERT_EQUAL_INT(FASTKV_OK,
        fastkv_put(db, FASTKV_STR("name"), FASTKV_STR("fastkv")));

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("name"), &val));
    TEST_ASSERT_EQUAL_UINT64(6, val.len);
    TEST_ASSERT_EQUAL_MEMORY("fastkv", val.data, 6);
}

void test_overwrite(void)
{
    fastkv_put(db, FASTKV_STR("k"), FASTKV_STR("v1"));
    fastkv_put(db, FASTKV_STR("k"), FASTKV_STR("v2"));

    fastkv_slice_t val;
    fastkv_get(db, FASTKV_STR("k"), &val);
    TEST_ASSERT_EQUAL_MEMORY("v2", val.data, 2);
}

void test_delete(void)
{
    fastkv_put(db, FASTKV_STR("gone"), FASTKV_STR("bye"));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_delete(db, FASTKV_STR("gone")));

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_get(db, FASTKV_STR("gone"), &val));
}

void test_get_missing(void)
{
    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_get(db, FASTKV_STR("nope"), &val));
}

void test_large_value(void)
{
    char *big = malloc(65536);
    TEST_ASSERT_NOT_NULL(big);
    memset(big, 'x', 65536);

    fastkv_slice_t val = FASTKV_SLICE(big, 65536);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_put(db, FASTKV_STR("big"), val));

    fastkv_slice_t got;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("big"), &got));
    TEST_ASSERT_EQUAL_UINT64(65536, got.len);
    free(big);
}

void test_many_keys(void)
{
    char key[32];
    for (int i = 0; i < 10000; i++) {
        snprintf(key, sizeof key, "key_%d", i);
        fastkv_put(db, FASTKV_STR(key), FASTKV_STR("val"));
    }
    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("key_9999"), &val));
}

void test_stats(void)
{
    fastkv_stats_t s;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_stats(db, &s));
    TEST_ASSERT_GREATER_OR_EQUAL(0UL, s.num_txn_committed);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_put_get);
    RUN_TEST(test_overwrite);
    RUN_TEST(test_delete);
    RUN_TEST(test_get_missing);
    RUN_TEST(test_large_value);
    RUN_TEST(test_many_keys);
    RUN_TEST(test_stats);
    return UNITY_END();
}
