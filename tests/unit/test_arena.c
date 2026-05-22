#include "unity.h"
#include "mem/arena.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_arena_basic_alloc(void)
{
 fastkv_arena_t *a = fastkv_arena_create(4096);
 TEST_ASSERT_NOT_NULL(a);

 void *p = fastkv_arena_alloc(a, 64);
 TEST_ASSERT_NOT_NULL(p);

 fastkv_arena_destroy(a);
}

void test_arena_alignment(void)
{
 fastkv_arena_t *a = fastkv_arena_create(4096);
 void *p = fastkv_arena_alloc_aligned(a, 1, 16);
 TEST_ASSERT_NOT_NULL(p);
 TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)p % 16);
 fastkv_arena_destroy(a);
}

void test_arena_overflow_to_new_block(void)
{
 fastkv_arena_t *a = fastkv_arena_create(128);
 /* Allocate more than the block size */
 void *p = fastkv_arena_alloc(a, 256);
 TEST_ASSERT_NOT_NULL(p);
 TEST_ASSERT_GREATER_THAN(128UL, a->total_allocated);
 fastkv_arena_destroy(a);
}

void test_arena_reset(void)
{
 fastkv_arena_t *a = fastkv_arena_create(4096);
 fastkv_arena_alloc(a, 512);
 fastkv_arena_reset(a);
 TEST_ASSERT_EQUAL_UINT64(0, a->total_allocated);
 /* Allocate again after reset */
 void *p = fastkv_arena_alloc(a, 64);
 TEST_ASSERT_NOT_NULL(p);
 fastkv_arena_destroy(a);
}

void test_arena_dup(void)
{
 fastkv_arena_t *a = fastkv_arena_create(4096);
 const char *src = "hello fastkv";
 char *dst = fastkv_arena_dup(a, src, strlen(src));
 TEST_ASSERT_NOT_NULL(dst);
 TEST_ASSERT_EQUAL_STRING(src, dst);
 fastkv_arena_destroy(a);
}

int main(void)
{
 UNITY_BEGIN();
 RUN_TEST(test_arena_basic_alloc);
 RUN_TEST(test_arena_alignment);
 RUN_TEST(test_arena_overflow_to_new_block);
 RUN_TEST(test_arena_reset);
 RUN_TEST(test_arena_dup);
 return UNITY_END();
}
