#include "unity.h"
#include "util/uuid7/uuid7.h"
#include "wal/wal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char   *tmp_dir = "/tmp/fastkv_test_wal";
static fastkv_wal_t *wal;
static uuid7_ctx     g_uuid7;

void setUp(void) {
    uuid7_init(&g_uuid7);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "mkdir -p %s", tmp_dir);
    system(cmd);
    fastkv_wal_open(&wal, tmp_dir, false, &g_uuid7);
}

void tearDown(void) {
    fastkv_wal_close(wal);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", tmp_dir);
    system(cmd);
}

void test_wal_open_close(void) {
    TEST_ASSERT_NOT_NULL(wal);
}

void test_wal_append_put(void) {
    fastkv_slice_t key   = FASTKV_STR("mykey");
    fastkv_slice_t value = FASTKV_STR("myvalue");
    fastkv_err_t   rc    = fastkv_wal_append(wal, WAL_REC_PUT, 42, key, value);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc);
    TEST_ASSERT_GREATER_THAN(0UL, fastkv_wal_bytes_written(wal));
}

void test_wal_append_delete(void) {
    fastkv_slice_t key = FASTKV_STR("gone");
    fastkv_err_t   rc  = fastkv_wal_append(wal, WAL_REC_DELETE, 99, key, FASTKV_SLICE_NULL);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc);
}

void test_wal_rotate(void) {
    fastkv_err_t rc = fastkv_wal_rotate(wal, &g_uuid7);
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_wal_open_close);
    RUN_TEST(test_wal_append_put);
    RUN_TEST(test_wal_append_delete);
    RUN_TEST(test_wal_rotate);
    return UNITY_END();
}
