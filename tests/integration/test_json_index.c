#include "fastkv.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fastkv_db_t    *db;
static fastkv_index_t *idx;
static const char     *db_path = "/tmp/fastkv_test_json_index";

static int found_count;

static fastkv_err_t count_cb(fastkv_slice_t pk, void *udata) {
    (void)pk;
    (void)udata;
    found_count++;
    return FASTKV_OK;
}

static char last_pk[256];

static fastkv_err_t collect_pk_cb(fastkv_slice_t pk, void *udata) {
    (void)udata;
    memcpy(last_pk, pk.data, pk.len);
    last_pk[pk.len] = '\0';
    found_count++;
    return FASTKV_OK;
}

void setUp(void) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "mkdir -p %s", db_path);
    system(cmd);

    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path          = db_path;
    opts.sync_writes   = false;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_open(&db, &opts));

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_json_index_create(db, "by_type", "type", &idx));

    found_count = 0;
    last_pk[0]  = '\0';
}

void tearDown(void) {
    fastkv_index_drop(db, "by_type");
    fastkv_close(db);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", db_path);
    system(cmd);
}

void test_json_lookup_string(void) {
    fastkv_put(db, FASTKV_STR("u:1"), FASTKV_STR("{\"type\":\"admin\",\"name\":\"ali\"}"));
    fastkv_put(db, FASTKV_STR("u:2"), FASTKV_STR("{\"type\":\"user\",\"name\":\"budi\"}"));
    fastkv_put(db, FASTKV_STR("u:3"), FASTKV_STR("{\"type\":\"admin\",\"name\":\"citra\"}"));

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_index_lookup(idx, FASTKV_STR("admin"), count_cb, NULL));
    TEST_ASSERT_EQUAL_INT(2, found_count);
}

void test_json_lookup_number(void) {
    fastkv_put(db, FASTKV_STR("p:1"), FASTKV_STR("{\"type\":42,\"data\":\"x\"}"));
    fastkv_put(db, FASTKV_STR("p:2"), FASTKV_STR("{\"type\":99,\"data\":\"y\"}"));

    TEST_ASSERT_EQUAL_INT(
        FASTKV_OK, fastkv_index_lookup(idx, FASTKV_STR("42"), collect_pk_cb, NULL));
    TEST_ASSERT_EQUAL_INT(1, found_count);
    TEST_ASSERT_EQUAL_STRING("p:1", last_pk);
}

void test_json_field_missing(void) {
    fastkv_put(db, FASTKV_STR("q:1"), FASTKV_STR("{\"name\":\"tanpa_type\"}"));

    TEST_ASSERT_EQUAL_INT(
        FASTKV_OK, fastkv_index_lookup(idx, FASTKV_STR("apapun"), count_cb, NULL));
    TEST_ASSERT_EQUAL_INT(0, found_count);
}

void test_json_delete_removes_entry(void) {
    fastkv_put(db, FASTKV_STR("d:1"), FASTKV_STR("{\"type\":\"temp\"}"));

    found_count = 0;
    fastkv_index_lookup(idx, FASTKV_STR("temp"), count_cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, found_count);

    fastkv_delete(db, FASTKV_STR("d:1"));

    found_count = 0;
    fastkv_index_lookup(idx, FASTKV_STR("temp"), count_cb, NULL);
    TEST_ASSERT_EQUAL_INT(0, found_count);
}

void test_json_update_changes_index(void) {
    fastkv_put(db, FASTKV_STR("e:1"), FASTKV_STR("{\"type\":\"lama\"}"));
    fastkv_put(db, FASTKV_STR("e:1"), FASTKV_STR("{\"type\":\"baru\"}"));

    found_count = 0;
    fastkv_index_lookup(idx, FASTKV_STR("lama"), count_cb, NULL);
    TEST_ASSERT_EQUAL_INT(0, found_count);

    found_count = 0;
    fastkv_index_lookup(idx, FASTKV_STR("baru"), count_cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, found_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_json_lookup_string);
    RUN_TEST(test_json_lookup_number);
    RUN_TEST(test_json_field_missing);
    RUN_TEST(test_json_delete_removes_entry);
    RUN_TEST(test_json_update_changes_index);
    return UNITY_END();
}
