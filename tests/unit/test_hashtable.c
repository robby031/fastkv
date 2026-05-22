#include "unity.h"
#include "storage/hashtable/ht.h"

#include <string.h>

static fastkv_ht_t *ht;

void setUp(void) { fastkv_ht_create(&ht, 64); }
void tearDown(void) { fastkv_ht_destroy(ht); }

void test_ht_put_get(void)
{
 fastkv_slice_t key = FASTKV_STR("hello");
 fastkv_slice_t value = FASTKV_STR("world");

 TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_ht_put(ht, 1, key, value));

 fastkv_slice_t got;
 TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_ht_get(ht, 2, key, &got));
 TEST_ASSERT_EQUAL_UINT64(value.len, got.len);
 TEST_ASSERT_EQUAL_MEMORY(value.data, got.data, value.len);
}

void test_ht_notfound(void)
{
 fastkv_slice_t key = FASTKV_STR("missing");
 fastkv_slice_t got;
 TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_ht_get(ht, 1, key, &got));
}

void test_ht_mvcc_snapshot_isolation(void)
{
 fastkv_slice_t key = FASTKV_STR("k");
 fastkv_slice_t v1  = FASTKV_STR("v1");
 fastkv_slice_t v2  = FASTKV_STR("v2");

 TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_ht_put(ht, 10, key, v1));
 TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_ht_put(ht, 20, key, v2));

 fastkv_slice_t got;
 /* snapshot at ts=15 should see v1 */
 TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_ht_get(ht, 15, key, &got));
 TEST_ASSERT_EQUAL_MEMORY(v1.data, got.data, v1.len);

 /* snapshot at ts=25 should see v2 */
 TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_ht_get(ht, 25, key, &got));
 TEST_ASSERT_EQUAL_MEMORY(v2.data, got.data, v2.len);
}

void test_ht_delete(void)
{
 fastkv_slice_t key = FASTKV_STR("del_me");
 fastkv_slice_t value = FASTKV_STR("data");

 fastkv_ht_put(ht, 1, key, value);
 fastkv_ht_delete(ht, 5, key);

 fastkv_slice_t got;
 TEST_ASSERT_EQUAL_INT(FASTKV_ERR_NOTFOUND, fastkv_ht_get(ht, 10, key, &got));
}

void test_ht_gc(void)
{
 fastkv_slice_t key = FASTKV_STR("gc_key");
 fastkv_ht_put(ht, 1, key, FASTKV_STR("old"));
 fastkv_ht_put(ht, 5, key, FASTKV_STR("new"));

 /* v1 was committed at ts=1, end_ts sealed to 5 by the second put.
 * We need min_active_ts > 5 for the old version to qualify for GC. */
 uint64_t freed = fastkv_ht_gc(ht, 6);
 TEST_ASSERT_GREATER_OR_EQUAL(1, freed);
}

int main(void)
{
 UNITY_BEGIN();
 RUN_TEST(test_ht_put_get);
 RUN_TEST(test_ht_notfound);
 RUN_TEST(test_ht_mvcc_snapshot_isolation);
 RUN_TEST(test_ht_delete);
 RUN_TEST(test_ht_gc);
 return UNITY_END();
}
