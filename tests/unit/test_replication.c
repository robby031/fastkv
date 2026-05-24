#include "fastkv.h"
#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char  *tmp_primary = "/tmp/fastkv_test_repl_primary";
static const char  *tmp_replica = "/tmp/fastkv_test_repl_replica";
static fastkv_db_t *primary;
static fastkv_db_t *replica;

static void cleanup_dirs(void) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf %s %s && mkdir -p %s %s", tmp_primary, tmp_replica,
        tmp_primary, tmp_replica);
    system(cmd);
}

void setUp(void) {
    cleanup_dirs();

    fastkv_opts_t opts = FASTKV_OPTS_DEFAULT;
    opts.sync_writes   = false;
    opts.map_size      = 1024;

    opts.path = tmp_primary;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_open(&primary, &opts));

    opts.path = tmp_replica;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_open(&replica, &opts));
}

void tearDown(void) {
    fastkv_repl_stop_server(primary);
    fastkv_repl_disconnect(replica);
    fastkv_close(primary);
    fastkv_close(replica);
    cleanup_dirs();
}

/* tunggu hingga kondisi terpenuhi atau timeout (detik) */
static bool wait_until(bool (*cond)(void *), void *arg, int timeout_sec) {
    for (int i = 0; i < timeout_sec * 10; i++) {
        if (cond(arg))
            return true;
        struct timespec ts = {.tv_nsec = 100 * 1000 * 1000}; /* 100 ms */
        nanosleep(&ts, NULL);
    }
    return false;
}

/* cek apakah replica terkonek */
typedef struct { fastkv_db_t *db; } conn_arg_t;
static bool replica_connected(void *arg) {
    conn_arg_t         *a    = arg;
    fastkv_repl_peer_t  stat = fastkv_repl_primary_stat(a->db);
    return stat.connected;
}

/* cek apakah key ada di replica */
typedef struct { fastkv_db_t *db; const char *key; const char *val; } key_arg_t;
static bool key_exists(void *arg) {
    key_arg_t     *a = arg;
    fastkv_slice_t v;
    if (fastkv_get(a->db, FASTKV_STR(a->key), &v) != FASTKV_OK)
        return false;
    return v.len == strlen(a->val) && memcmp(v.data, a->val, v.len) == 0;
}
static bool key_absent(void *arg) {
    key_arg_t     *a = arg;
    fastkv_slice_t v;
    return fastkv_get(a->db, FASTKV_STR(a->key), &v) == FASTKV_ERR_NOTFOUND;
}

void test_repl_serve_connect(void) {
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_repl_serve(primary, 17701));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_repl_connect(replica, "127.0.0.1", 17701));

    conn_arg_t a = {.db = replica};
    TEST_ASSERT_TRUE(wait_until(replica_connected, &a, 5));

    fastkv_repl_peer_t stat = fastkv_repl_primary_stat(replica);
    TEST_ASSERT_TRUE(stat.connected);
}

void test_repl_put_propagates(void) {
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_repl_serve(primary, 17702));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_repl_connect(replica, "127.0.0.1", 17702));

    conn_arg_t ca = {.db = replica};
    TEST_ASSERT_TRUE(wait_until(replica_connected, &ca, 5));

    /* tulis di primary */
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_put(primary, FASTKV_STR("hello"), FASTKV_STR("world")));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK,
        fastkv_put(primary, FASTKV_STR("foo"), FASTKV_STR("bar")));

    /* tunggu sampai muncul di replica */
    key_arg_t ka1 = {.db = replica, .key = "hello", .val = "world"};
    key_arg_t ka2 = {.db = replica, .key = "foo", .val = "bar"};
    TEST_ASSERT_TRUE(wait_until(key_exists, &ka1, 5));
    TEST_ASSERT_TRUE(wait_until(key_exists, &ka2, 5));
}

void test_repl_delete_propagates(void) {
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_repl_serve(primary, 17703));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_repl_connect(replica, "127.0.0.1", 17703));

    conn_arg_t ca = {.db = replica};
    TEST_ASSERT_TRUE(wait_until(replica_connected, &ca, 5));

    fastkv_put(primary, FASTKV_STR("temp"), FASTKV_STR("value"));

    key_arg_t ka = {.db = replica, .key = "temp", .val = "value"};
    TEST_ASSERT_TRUE(wait_until(key_exists, &ka, 5));

    /* hapus di primary */
    fastkv_delete(primary, FASTKV_STR("temp"));

    TEST_ASSERT_TRUE(wait_until(key_absent, &ka, 5));
}

void test_repl_peers_stat(void) {
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_repl_serve(primary, 17704));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_repl_connect(replica, "127.0.0.1", 17704));

    conn_arg_t ca = {.db = replica};
    TEST_ASSERT_TRUE(wait_until(replica_connected, &ca, 5));

    /* tulis beberapa kunci supaya ada data mengalir */
    for (int i = 0; i < 100; i++) {
        char k[16], v[16];
        snprintf(k, sizeof k, "key%d", i);
        snprintf(v, sizeof v, "val%d", i);
        fastkv_put(primary, FASTKV_SLICE(k, strlen(k)), FASTKV_SLICE(v, strlen(v)));
    }

    /* tunggu replica menyelesaikan */
    key_arg_t ka = {.db = replica, .key = "key99", .val = "val99"};
    TEST_ASSERT_TRUE(wait_until(key_exists, &ka, 5));

    /* cek stats dari sisi primary */
    fastkv_repl_peer_t peers[4];
    size_t             n = 0;
    fastkv_repl_peers(primary, peers, 4, &n);
    TEST_ASSERT_EQUAL_INT(1, (int)n);
    TEST_ASSERT_TRUE(peers[0].connected);

    /* cek stats dari sisi replica */
    fastkv_repl_peer_t pstat = fastkv_repl_primary_stat(replica);
    TEST_ASSERT_TRUE(pstat.connected);
    TEST_ASSERT_GREATER_THAN(0ULL, pstat.bytes_total);
}

void test_repl_multi_put(void) {
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_repl_serve(primary, 17705));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_repl_connect(replica, "127.0.0.1", 17705));

    conn_arg_t ca = {.db = replica};
    TEST_ASSERT_TRUE(wait_until(replica_connected, &ca, 5));

    /* 1000 kunci */
    for (int i = 0; i < 1000; i++) {
        char k[24], v[24];
        snprintf(k, sizeof k, "mkey%04d", i);
        snprintf(v, sizeof v, "mval%04d", i);
        fastkv_put(primary, FASTKV_SLICE(k, strlen(k)), FASTKV_SLICE(v, strlen(v)));
    }

    key_arg_t ka = {.db = replica, .key = "mkey0999", .val = "mval0999"};
    TEST_ASSERT_TRUE(wait_until(key_exists, &ka, 10));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_repl_serve_connect);
    RUN_TEST(test_repl_put_propagates);
    RUN_TEST(test_repl_delete_propagates);
    RUN_TEST(test_repl_peers_stat);
    RUN_TEST(test_repl_multi_put);
    return UNITY_END();
}
