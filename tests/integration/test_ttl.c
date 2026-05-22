#include "fastkv.h"

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static fastkv_db_t *db;
static const char  *db_path = "/tmp/fastkv_test_ttl";

static void sleep_ms(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

void setUp(void)
{
    char cmd[256];
    snprintf(cmd, sizeof cmd, "mkdir -p %s", db_path);
    system(cmd);

    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.path = db_path;
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

void test_put_ttl_key_visible_before_expiry(void)
{
    TEST_ASSERT_EQUAL_INT(FASTKV_OK,
        fastkv_put_ttl(db, FASTKV_STR("ttl_key"), FASTKV_STR("nilai"), 5000));

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("ttl_key"), &val));
    TEST_ASSERT_EQUAL_size_t(5, val.len);
    fastkv_free_value(db, &val);
}

void test_put_ttl_key_expired_after_sync(void)
{
    /* TTL 100ms — kunci harus hilang setelah TTL berakhir dan checkpoint */
    TEST_ASSERT_EQUAL_INT(FASTKV_OK,
        fastkv_put_ttl(db, FASTKV_STR("temp"), FASTKV_STR("sementara"), 100));

    /* pastikan kunci ada dulu */
    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("temp"), &val));
    fastkv_free_value(db, &val);

    /* tunggu TTL habis */
    sleep_ms(200);

    /* trigger sync + checkpoint yang akan memanggil fastkv_ttl_expire */
    fastkv_sync(db);

    /* kunci harus sudah dihapus */
    TEST_ASSERT_NOT_EQUAL(FASTKV_OK, fastkv_get(db, FASTKV_STR("temp"), &val));
}

void test_ttl_renew(void)
{
    /* tulis dengan TTL pendek, lalu perpanjang */
    TEST_ASSERT_EQUAL_INT(FASTKV_OK,
        fastkv_put_ttl(db, FASTKV_STR("renew"), FASTKV_STR("v"), 100));
    sleep_ms(50);
    /* perpanjang sebelum expired */
    TEST_ASSERT_EQUAL_INT(FASTKV_OK,
        fastkv_put_ttl(db, FASTKV_STR("renew"), FASTKV_STR("v2"), 5000));

    /* tunggu TTL awal habis */
    sleep_ms(100);
    fastkv_sync(db);

    /* kunci masih harus ada karena sudah di-renew */
    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("renew"), &val));
    fastkv_free_value(db, &val);
}

void test_normal_put_not_expired(void)
{
    /* put biasa tanpa TTL tidak boleh terhapus */
    fastkv_put(db, FASTKV_STR("permanent"), FASTKV_STR("tetap"));

    sleep_ms(50);
    fastkv_sync(db);

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("permanent"), &val));
    fastkv_free_value(db, &val);
}

void test_multiple_ttl_keys(void)
{
    fastkv_put_ttl(db, FASTKV_STR("short:1"), FASTKV_STR("a"), 100);
    fastkv_put_ttl(db, FASTKV_STR("short:2"), FASTKV_STR("b"), 100);
    fastkv_put_ttl(db, FASTKV_STR("long:1"), FASTKV_STR("c"), 10000);

    sleep_ms(200);
    fastkv_sync(db);

    fastkv_slice_t val;
    TEST_ASSERT_NOT_EQUAL(FASTKV_OK, fastkv_get(db, FASTKV_STR("short:1"), &val));
    TEST_ASSERT_NOT_EQUAL(FASTKV_OK, fastkv_get(db, FASTKV_STR("short:2"), &val));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR("long:1"), &val));
    fastkv_free_value(db, &val);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_put_ttl_key_visible_before_expiry);
    RUN_TEST(test_put_ttl_key_expired_after_sync);
    RUN_TEST(test_ttl_renew);
    RUN_TEST(test_normal_put_not_expired);
    RUN_TEST(test_multiple_ttl_keys);
    return UNITY_END();
}
