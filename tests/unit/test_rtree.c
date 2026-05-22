#include "unity.h"
#include "index/rtree/rtree.h"

#include <math.h>
#include <string.h>

static fastkv_rtree_t *tree;

void setUp(void)
{
    fastkv_rtree_create(&tree, 2);
}

void tearDown(void)
{
    fastkv_rtree_destroy(tree);
}

static fastkv_rect_t rect2(double x1, double y1, double x2, double y2)
{
    fastkv_rect_t r;
    r.ndims = 2;
    r.min[0] = x1; r.min[1] = y1;
    r.max[0] = x2; r.max[1] = y2;
    return r;
}

static fastkv_rect_t point2(double x, double y)
{
    return rect2(x, y, x, y);
}

typedef struct { char keys[16][32]; int n; } result_t;

static fastkv_err_t collect(fastkv_rect_t rect, fastkv_slice_t key, void *ud)
{
    (void)rect;
    result_t *r = ud;
    if (r->n < 16) {
        memcpy(r->keys[r->n], key.data, key.len);
        r->keys[r->n][key.len] = '\0';
        r->n++;
    }
    return FASTKV_OK;
}

void test_insert_and_intersects(void)
{
    fastkv_rtree_insert(tree, point2(1, 1), FASTKV_STR("p1"));
    fastkv_rtree_insert(tree, point2(5, 5), FASTKV_STR("p2"));
    fastkv_rtree_insert(tree, point2(9, 9), FASTKV_STR("p3"));

    result_t r = {0};
    fastkv_rect_t q = rect2(0, 0, 6, 6);
    fastkv_rtree_intersects(tree, q, collect, &r);

    TEST_ASSERT_EQUAL_INT(2, r.n);
}

void test_within(void)
{
    fastkv_rtree_insert(tree, point2(2, 2), FASTKV_STR("inside"));
    fastkv_rtree_insert(tree, point2(8, 8), FASTKV_STR("outside"));
    fastkv_rtree_insert(tree, rect2(0, 0, 10, 10), FASTKV_STR("big"));

    result_t r = {0};
    fastkv_rect_t q = rect2(1, 1, 5, 5);
    fastkv_rtree_within(tree, q, collect, &r);

    /* hanya "inside" benar-benar di dalam [1,5]x[1,5] */
    TEST_ASSERT_EQUAL_INT(1, r.n);
    TEST_ASSERT_EQUAL_STRING("inside", r.keys[0]);
}

void test_delete(void)
{
    fastkv_rect_t p = point2(3, 3);
    fastkv_rtree_insert(tree, p, FASTKV_STR("target"));
    fastkv_rtree_insert(tree, point2(7, 7), FASTKV_STR("other"));

    TEST_ASSERT_EQUAL_INT(FASTKV_OK, fastkv_rtree_delete(tree, p, FASTKV_STR("target")));

    result_t r = {0};
    fastkv_rtree_intersects(tree, rect2(0, 0, 10, 10), collect, &r);
    TEST_ASSERT_EQUAL_INT(1, r.n);
    TEST_ASSERT_EQUAL_STRING("other", r.keys[0]);
}

void test_nearby_order(void)
{
    fastkv_rtree_insert(tree, point2(10, 10), FASTKV_STR("far"));
    fastkv_rtree_insert(tree, point2(1, 1),   FASTKV_STR("near"));
    fastkv_rtree_insert(tree, point2(5, 5),   FASTKV_STR("mid"));

    result_t r = {0};
    fastkv_coord_t origin[2] = {0.0, 0.0};
    fastkv_rtree_nearby(tree, origin, 3, collect, &r);

    TEST_ASSERT_EQUAL_INT(3, r.n);
    /* yang paling dekat harus "near" (jarak sqrt(2)) */
    TEST_ASSERT_EQUAL_STRING("near", r.keys[0]);
}

void test_nearby_limit(void)
{
    for (int i = 1; i <= 10; i++) {
        char name[8];
        snprintf(name, sizeof name, "p%d", i);
        fastkv_rtree_insert(tree, point2((double)i, 0), FASTKV_STR(name));
    }

    result_t r = {0};
    fastkv_coord_t origin[2] = {0.0, 0.0};
    fastkv_rtree_nearby(tree, origin, 3, collect, &r);

    TEST_ASSERT_EQUAL_INT(3, r.n);
    /* 3 terdekat: p1, p2, p3 */
    TEST_ASSERT_EQUAL_STRING("p1", r.keys[0]);
    TEST_ASSERT_EQUAL_STRING("p2", r.keys[1]);
    TEST_ASSERT_EQUAL_STRING("p3", r.keys[2]);
}

void test_many_inserts(void)
{
    /* sisipkan banyak titik untuk memicu split */
    for (int i = 0; i < 50; i++) {
        char name[8];
        snprintf(name, sizeof name, "p%d", i);
        fastkv_rtree_insert(tree, point2((double)i, (double)i), FASTKV_STR(name));
    }

    result_t r = {0};
    /* ambil semua yang ada di [0,100]x[0,100] */
    fastkv_rtree_intersects(tree, rect2(0, 0, 100, 100), collect, &r);
    /* collected_t hanya muat 16, tapi harus ada lebih dari itu */
    TEST_ASSERT_EQUAL_INT(16, r.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_insert_and_intersects);
    RUN_TEST(test_within);
    RUN_TEST(test_delete);
    RUN_TEST(test_nearby_order);
    RUN_TEST(test_nearby_limit);
    RUN_TEST(test_many_inserts);
    return UNITY_END();
}
