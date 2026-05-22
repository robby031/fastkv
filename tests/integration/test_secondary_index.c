#include "fastkv.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fastkv_db_t    *db;
static fastkv_index_t *idx;
static const char     *db_path = "/tmp/fastkv_test_secondary";

/* ekstrak field "cat" dari nilai sebagai index key */
static int category_fn(
    fastkv_slice_t key, fastkv_slice_t value, fastkv_slice_t *ik_out, void *udata) {
    (void)key;
    (void)udata;

    /* format nilai sederhana: "cat:<kategori>;" */
    const char *data = (const char *)value.data;
    const char *p    = strstr(data, "cat:");
    if (!p)
        return -1;
    p += 4;
    const char *end = strchr(p, ';');
    size_t      len = end ? (size_t)(end - p) : strlen(p);
    *ik_out         = FASTKV_SLICE(p, len);
    return 0;
}

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

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_index_create(db, "by_cat", category_fn, NULL, &idx));

    found_count = 0;
    last_pk[0]  = '\0';
}

void tearDown(void) {
    fastkv_index_drop(db, "by_cat");
    fastkv_close(db);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", db_path);
    system(cmd);
}

void test_lookup_basic(void) {
    fastkv_put(db, FASTKV_STR("item:1"), FASTKV_STR("cat:elektronik;name:laptop"));
    fastkv_put(db, FASTKV_STR("item:2"), FASTKV_STR("cat:elektronik;name:mouse"));
    fastkv_put(db, FASTKV_STR("item:3"), FASTKV_STR("cat:buku;name:novel"));

    TEST_ASSERT_EQUAL_INT(
        FASTKV_OK, fastkv_index_lookup(idx, FASTKV_STR("elektronik"), count_cb, NULL));
    TEST_ASSERT_EQUAL_INT(2, found_count);
}

void test_lookup_single(void) {
    fastkv_put(db, FASTKV_STR("p:a"), FASTKV_STR("cat:buku;"));
    fastkv_put(db, FASTKV_STR("p:b"), FASTKV_STR("cat:makanan;"));

    TEST_ASSERT_EQUAL_INT(
        FASTKV_OK, fastkv_index_lookup(idx, FASTKV_STR("buku"), collect_pk_cb, NULL));
    TEST_ASSERT_EQUAL_INT(1, found_count);
    TEST_ASSERT_EQUAL_STRING("p:a", last_pk);
}

void test_lookup_no_match(void) {
    fastkv_put(db, FASTKV_STR("x"), FASTKV_STR("cat:buku;"));

    TEST_ASSERT_EQUAL_INT(
        FASTKV_OK, fastkv_index_lookup(idx, FASTKV_STR("elektronik"), count_cb, NULL));
    TEST_ASSERT_EQUAL_INT(0, found_count);
}

void test_range_scan(void) {
    fastkv_put(db, FASTKV_STR("r:1"), FASTKV_STR("cat:a;"));
    fastkv_put(db, FASTKV_STR("r:2"), FASTKV_STR("cat:b;"));
    fastkv_put(db, FASTKV_STR("r:3"), FASTKV_STR("cat:c;"));
    fastkv_put(db, FASTKV_STR("r:4"), FASTKV_STR("cat:z;"));

    TEST_ASSERT_EQUAL_INT(
        FASTKV_OK, fastkv_index_range(idx, FASTKV_STR("a"), FASTKV_STR("c"), count_cb, NULL));
    TEST_ASSERT_EQUAL_INT(3, found_count);
}

void test_delete_removes_from_index(void) {
    fastkv_put(db, FASTKV_STR("del:1"), FASTKV_STR("cat:hapus;"));
    fastkv_put(db, FASTKV_STR("del:2"), FASTKV_STR("cat:hapus;"));

    found_count = 0;
    fastkv_index_lookup(idx, FASTKV_STR("hapus"), count_cb, NULL);
    TEST_ASSERT_EQUAL_INT(2, found_count);

    fastkv_delete(db, FASTKV_STR("del:1"));

    found_count = 0;
    fastkv_index_lookup(idx, FASTKV_STR("hapus"), count_cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, found_count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lookup_basic);
    RUN_TEST(test_lookup_single);
    RUN_TEST(test_lookup_no_match);
    RUN_TEST(test_range_scan);
    RUN_TEST(test_delete_removes_from_index);
    return UNITY_END();
}
