#include "index/btree/btree.h"
#include "unity.h"

#include <string.h>

static fastkv_btree_t *tree;

void setUp(void) {
    fastkv_btree_create(&tree);
}

void tearDown(void) {
    fastkv_btree_destroy(tree);
}

static fastkv_slice_t str(const char *s) {
    return FASTKV_STR(s);
}

void test_insert_and_get(void) {
    fastkv_btree_insert(tree, str("b"), str("2"));
    fastkv_btree_insert(tree, str("a"), str("1"));
    fastkv_btree_insert(tree, str("c"), str("3"));

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_btree_get(tree, str("a"), &val));
    TEST_ASSERT_EQUAL_MEMORY("1", val.data, 1);

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_btree_get(tree, str("b"), &val));
    TEST_ASSERT_EQUAL_MEMORY("2", val.data, 1);

    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_btree_get(tree, str("z"), &val));
}

void test_update_existing_key(void) {
    fastkv_btree_insert(tree, str("k"), str("old"));
    fastkv_btree_insert(tree, str("k"), str("new"));

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_btree_get(tree, str("k"), &val));
    TEST_ASSERT_EQUAL_MEMORY("new", val.data, 3);
}

void test_delete(void) {
    fastkv_btree_insert(tree, str("x"), str("val"));
    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_btree_delete(tree, str("x")));
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_btree_delete(tree, str("x")));

    fastkv_slice_t val;
    TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_btree_get(tree, str("x"), &val));
}

typedef struct {
    char keys[16][32];
    int  n;
} collected_t;

static fastkv_err_t collect(fastkv_slice_t key, fastkv_slice_t val, void *ud) {
    (void)val;
    collected_t *c = ud;
    if (c->n < 16) {
        memcpy(c->keys[c->n], key.data, key.len);
        c->keys[c->n][key.len] = '\0';
        c->n++;
    }
    return FASTKV_OK;
}

void test_scan_forward_order(void) {
    fastkv_btree_insert(tree, str("dog"), str("1"));
    fastkv_btree_insert(tree, str("ant"), str("2"));
    fastkv_btree_insert(tree, str("cat"), str("3"));
    fastkv_btree_insert(tree, str("eel"), str("4"));

    collected_t c = {0};
    fastkv_btree_scan(
        tree, FASTKV_SLICE_NULL, FASTKV_SLICE_NULL, FASTKV_CURSOR_FORWARD, collect, &c);

    TEST_ASSERT_EQUAL_INT(4, c.n);
    TEST_ASSERT_EQUAL_STRING("ant", c.keys[0]);
    TEST_ASSERT_EQUAL_STRING("cat", c.keys[1]);
    TEST_ASSERT_EQUAL_STRING("dog", c.keys[2]);
    TEST_ASSERT_EQUAL_STRING("eel", c.keys[3]);
}

void test_scan_backward_order(void) {
    fastkv_btree_insert(tree, str("dog"), str("1"));
    fastkv_btree_insert(tree, str("ant"), str("2"));
    fastkv_btree_insert(tree, str("cat"), str("3"));

    collected_t c = {0};
    fastkv_btree_scan(
        tree, FASTKV_SLICE_NULL, FASTKV_SLICE_NULL, FASTKV_CURSOR_BACKWARD, collect, &c);

    TEST_ASSERT_EQUAL_INT(3, c.n);
    TEST_ASSERT_EQUAL_STRING("dog", c.keys[0]);
    TEST_ASSERT_EQUAL_STRING("cat", c.keys[1]);
    TEST_ASSERT_EQUAL_STRING("ant", c.keys[2]);
}

void test_scan_range(void) {
    fastkv_btree_insert(tree, str("a"), str("1"));
    fastkv_btree_insert(tree, str("b"), str("2"));
    fastkv_btree_insert(tree, str("c"), str("3"));
    fastkv_btree_insert(tree, str("d"), str("4"));
    fastkv_btree_insert(tree, str("e"), str("5"));

    collected_t c = {0};
    fastkv_btree_scan(tree, str("b"), str("d"), FASTKV_CURSOR_FORWARD, collect, &c);

    TEST_ASSERT_EQUAL_INT(3, c.n);
    TEST_ASSERT_EQUAL_STRING("b", c.keys[0]);
    TEST_ASSERT_EQUAL_STRING("c", c.keys[1]);
    TEST_ASSERT_EQUAL_STRING("d", c.keys[2]);
}

void test_large_insert_stays_sorted(void) {
    /* sisipkan lebih dari BTREE_ORDER entri untuk memicu split */
    char key[8], val[8];
    for (int i = 99; i >= 0; i--) {
        snprintf(key, sizeof key, "%03d", i);
        snprintf(val, sizeof val, "v%d", i);
        fastkv_btree_insert(tree, FASTKV_STR(key), FASTKV_STR(val));
    }

    collected_t c = {0};
    fastkv_btree_scan(
        tree, FASTKV_SLICE_NULL, FASTKV_SLICE_NULL, FASTKV_CURSOR_FORWARD, collect, &c);

    TEST_ASSERT_EQUAL_INT(16, c.n); /* hanya 16 yang bisa ditampung di collected_t */
    TEST_ASSERT_EQUAL_STRING("000", c.keys[0]);
    TEST_ASSERT_EQUAL_STRING("001", c.keys[1]);
    TEST_ASSERT_EQUAL_STRING("015", c.keys[15]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_insert_and_get);
    RUN_TEST(test_update_existing_key);
    RUN_TEST(test_delete);
    RUN_TEST(test_scan_forward_order);
    RUN_TEST(test_scan_backward_order);
    RUN_TEST(test_scan_range);
    RUN_TEST(test_large_insert_stays_sorted);
    return UNITY_END();
}
