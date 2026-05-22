#include "unity.h"
#include "util/crc32.h"

void setUp(void) {}
void tearDown(void) {}

void test_crc32c_known_value(void) {
    /* CRC-32C of "123456789" = 0xE3069283 */
    uint32_t crc = fastkv_crc32c_buf("123456789", 9);
    TEST_ASSERT_EQUAL_HEX32(0xE3069283, crc);
}

void test_crc32c_empty(void) {
    TEST_ASSERT_EQUAL_HEX32(0x00000000, fastkv_crc32c_buf("", 0));
}

void test_crc32c_incremental(void) {
    const char *msg  = "hello world";
    uint32_t    full = fastkv_crc32c_buf(msg, 11);
    uint32_t    inc  = fastkv_crc32c(0, msg, 5);
    inc              = fastkv_crc32c(inc, msg + 5, 6);
    TEST_ASSERT_EQUAL_HEX32(full, inc);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32c_known_value);
    RUN_TEST(test_crc32c_empty);
    RUN_TEST(test_crc32c_incremental);
    return UNITY_END();
}
