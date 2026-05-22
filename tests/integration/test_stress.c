#include "fastkv.h"

#include "unity.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fastkv_db_t *db;
static const char  *db_path = "/tmp/fastkv_test_stress";

#define NUM_WRITERS 4
#define NUM_READERS 4
#define OPS_PER_THREAD 200

static _Atomic int write_errors;
static _Atomic int read_errors;

static void *writer_fn(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < OPS_PER_THREAD; i++) {
        char key[64], val[64];
        snprintf(key, sizeof key, "w%d:key%d", id, i);
        snprintf(val, sizeof val, "val_%d_%d", id, i);
        fastkv_err_t rc = fastkv_put(db, FASTKV_STR(key), FASTKV_STR(val));
        if (rc != FASTKV_OK)
            atomic_fetch_add(&write_errors, 1);
    }
    return NULL;
}

static void *reader_fn(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < OPS_PER_THREAD; i++) {
        /* baca kunci acak dari writer mana saja */
        int w = i % NUM_WRITERS;
        int k = i % OPS_PER_THREAD;
        char key[64];
        snprintf(key, sizeof key, "w%d:key%d", w, k);

        fastkv_slice_t val;
        fastkv_err_t rc = fastkv_get(db, FASTKV_STR(key), &val);
        /* boleh NOT_FOUND — tergantung urutan eksekusi */
        if (rc != FASTKV_OK && rc != FASTKV_ERR_NOTFOUND)
            atomic_fetch_add(&read_errors, 1);
        if (rc == FASTKV_OK)
            fastkv_free_value(db, &val);
    }
    (void)id;
    return NULL;
}

static void *txn_writer_fn(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < OPS_PER_THREAD / 10; i++) {
        fastkv_txn_t *txn;
        fastkv_err_t  rc = fastkv_txn_begin(db, false, &txn);
        if (rc != FASTKV_OK) {
            atomic_fetch_add(&write_errors, 1);
            continue;
        }

        char key[64], val[64];
        snprintf(key, sizeof key, "txn%d:k%d", id, i);
        snprintf(val, sizeof val, "txnval_%d", i);
        fastkv_txn_put(txn, FASTKV_STR(key), FASTKV_STR(val));

        rc = fastkv_txn_commit(txn);
        /* conflict bisa terjadi, itu normal */
        if (rc != FASTKV_OK && rc != FASTKV_ERR_TXN_CONFLICT)
            atomic_fetch_add(&write_errors, 1);
    }
    return NULL;
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

    atomic_store(&write_errors, 0);
    atomic_store(&read_errors, 0);
}

void tearDown(void)
{
    fastkv_close(db);
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", db_path);
    system(cmd);
}

void test_concurrent_writers(void)
{
    pthread_t threads[NUM_WRITERS];
    for (int i = 0; i < NUM_WRITERS; i++)
        pthread_create(&threads[i], NULL, writer_fn, (void *)(intptr_t)i);
    for (int i = 0; i < NUM_WRITERS; i++)
        pthread_join(threads[i], NULL);

    TEST_ASSERT_EQUAL_INT(0, atomic_load(&write_errors));

    /* verifikasi semua kunci bisa dibaca */
    for (int w = 0; w < NUM_WRITERS; w++) {
        for (int k = 0; k < OPS_PER_THREAD; k++) {
            char key[64];
            snprintf(key, sizeof key, "w%d:key%d", w, k);
            fastkv_slice_t val;
            TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_get(db, FASTKV_STR(key), &val));
            fastkv_free_value(db, &val);
        }
    }
}

void test_concurrent_readers_writers(void)
{
    /* tulis dulu beberapa kunci supaya reader punya sesuatu */
    for (int i = 0; i < NUM_WRITERS; i++) {
        for (int k = 0; k < 10; k++) {
            char key[64], val[64];
            snprintf(key, sizeof key, "w%d:key%d", i, k);
            snprintf(val, sizeof val, "init_%d_%d", i, k);
            fastkv_put(db, FASTKV_STR(key), FASTKV_STR(val));
        }
    }

    pthread_t wt[NUM_WRITERS], rt[NUM_READERS];
    for (int i = 0; i < NUM_WRITERS; i++)
        pthread_create(&wt[i], NULL, writer_fn, (void *)(intptr_t)i);
    for (int i = 0; i < NUM_READERS; i++)
        pthread_create(&rt[i], NULL, reader_fn, (void *)(intptr_t)i);

    for (int i = 0; i < NUM_WRITERS; i++) pthread_join(wt[i], NULL);
    for (int i = 0; i < NUM_READERS; i++) pthread_join(rt[i], NULL);

    TEST_ASSERT_EQUAL_INT(0, atomic_load(&write_errors));
    TEST_ASSERT_EQUAL_INT(0, atomic_load(&read_errors));
}

void test_concurrent_transactions(void)
{
    pthread_t threads[NUM_WRITERS];
    for (int i = 0; i < NUM_WRITERS; i++)
        pthread_create(&threads[i], NULL, txn_writer_fn, (void *)(intptr_t)i);
    for (int i = 0; i < NUM_WRITERS; i++)
        pthread_join(threads[i], NULL);

    TEST_ASSERT_EQUAL_INT(0, atomic_load(&write_errors));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_concurrent_writers);
    RUN_TEST(test_concurrent_readers_writers);
    RUN_TEST(test_concurrent_transactions);
    return UNITY_END();
}
