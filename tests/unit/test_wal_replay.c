#include "fastkv/types.h"

#include "unity.h"
#include "util/uuid7/uuid7.h"
#include "wal/wal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char   *tmp = "/tmp/fastkv_test_wal_replay";
static fastkv_wal_t *wal;
static uuid7_ctx     g_uuid7;

void setUp(void) {
    uuid7_init(&g_uuid7);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", tmp, tmp);
    system(cmd);
    fastkv_wal_open(&wal, tmp, false, &g_uuid7);
}

void tearDown(void) {
    fastkv_wal_close(wal);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", tmp);
    system(cmd);
}

/* Replay callback accumulates records into a flat array */
typedef struct {
    fastkv_ts_t ts;
    char        key[64];
    char        val[64];
} rec_t;
typedef struct {
    rec_t buf[64];
    int   n;
} acc_t;

static fastkv_err_t collect(const fastkv_wal_record_t *r, void *ud) {
    acc_t *a = (acc_t *)ud;
    if (a->n >= 64)
        return FASTKV_OK;
    rec_t *e = &a->buf[a->n++];
    e->ts    = r->ts;
    memcpy(e->key, r->key.data, r->key.len);
    e->key[r->key.len] = '\0';
    if (r->value.data) {
        memcpy(e->val, r->value.data, r->value.len);
        e->val[r->value.len] = '\0';
    } else {
        strcpy(e->val, "<tombstone>");
    }
    return FASTKV_OK;
}

void test_replay_basic(void) {
    fastkv_wal_append(wal, WAL_REC_PUT, 10, FASTKV_STR("alpha"), FASTKV_STR("A"));
    fastkv_wal_append(wal, WAL_REC_PUT, 20, FASTKV_STR("beta"), FASTKV_STR("B"));
    fastkv_wal_append(wal, WAL_REC_DELETE, 30, FASTKV_STR("alpha"), FASTKV_SLICE_NULL);
    fastkv_wal_close(wal);
    wal = NULL;

    acc_t        acc    = {0};
    fastkv_ts_t  max_ts = 0;
    fastkv_err_t rc     = fastkv_wal_replay(tmp, 0, collect, &acc, &max_ts);

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc);
    TEST_ASSERT_EQUAL_INT(3, acc.n);
    TEST_ASSERT_EQUAL_UINT64(30, max_ts);
    TEST_ASSERT_EQUAL_STRING("alpha", acc.buf[0].key);
    TEST_ASSERT_EQUAL_STRING("beta", acc.buf[1].key);
    TEST_ASSERT_EQUAL_STRING("alpha", acc.buf[2].key);
    TEST_ASSERT_EQUAL_STRING("<tombstone>", acc.buf[2].val);

    /* re-open for tearDown */
    fastkv_wal_open(&wal, tmp, false, &g_uuid7);
}

void test_replay_since_ts(void) {
    fastkv_wal_append(wal, WAL_REC_PUT, 5, FASTKV_STR("old"), FASTKV_STR("v"));
    fastkv_wal_append(wal, WAL_REC_PUT, 15, FASTKV_STR("new"), FASTKV_STR("v"));
    fastkv_wal_close(wal);
    wal = NULL;

    acc_t       acc    = {0};
    fastkv_ts_t max_ts = 0;
    fastkv_wal_replay(tmp, 10 /* since_ts */, collect, &acc, &max_ts);

    /* only the record at ts=15 should be replayed */
    TEST_ASSERT_EQUAL_INT(1, acc.n);
    TEST_ASSERT_EQUAL_STRING("new", acc.buf[0].key);
    TEST_ASSERT_EQUAL_UINT64(15, max_ts);

    fastkv_wal_open(&wal, tmp, false, &g_uuid7);
}

void test_replay_multi_segment(void) {
    fastkv_wal_append(wal, WAL_REC_PUT, 1, FASTKV_STR("k1"), FASTKV_STR("v1"));
    fastkv_wal_rotate(wal, &g_uuid7); /* new segment */
    fastkv_wal_append(wal, WAL_REC_PUT, 2, FASTKV_STR("k2"), FASTKV_STR("v2"));
    fastkv_wal_close(wal);
    wal = NULL;

    acc_t       acc    = {0};
    fastkv_ts_t max_ts = 0;
    fastkv_wal_replay(tmp, 0, collect, &acc, &max_ts);

    TEST_ASSERT_EQUAL_INT(2, acc.n);
    TEST_ASSERT_EQUAL_UINT64(2, max_ts);

    fastkv_wal_open(&wal, tmp, false, &g_uuid7);
}

void test_replay_empty_dir(void) {
    fastkv_wal_close(wal);
    wal = NULL;

    /* Remove the segment created by setUp so the dir is really empty of WAL files */
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -f %s/wal-*.log", tmp);
    system(cmd);

    acc_t        acc    = {0};
    fastkv_ts_t  max_ts = 99;
    fastkv_err_t rc     = fastkv_wal_replay(tmp, 0, collect, &acc, &max_ts);

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, rc);
    TEST_ASSERT_EQUAL_INT(0, acc.n);
    TEST_ASSERT_EQUAL_UINT64(99, max_ts); /* unchanged */

    fastkv_wal_open(&wal, tmp, false, &g_uuid7);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_replay_basic);
    RUN_TEST(test_replay_since_ts);
    RUN_TEST(test_replay_multi_segment);
    RUN_TEST(test_replay_empty_dir);
    return UNITY_END();
}
