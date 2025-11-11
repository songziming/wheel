#include <gtest/gtest.h>

extern "C" {
    #include <string.h>
    #include "kstring.h"
}

TEST(String, Memset) {
    uint32_t buff[8];
    memset(buff, 0xbe, sizeof(buff));
    kmemset((uint8_t*)buff + 3, 0xad, sizeof(uint32_t) * 7 - 1);
    EXPECT_EQ(buff[0], 0xadbebebe);
    EXPECT_EQ(buff[7], 0xbebeadad);
}

TEST(String, Memcpy) {
    uint32_t arr32[8];
    uint8_t *arr8 = (uint8_t*)arr32;
    for (int i = 0; i < sizeof(arr32); ++i) {
        arr8[i] = i;
    }

    kmemcpy(arr8, arr8, 4);
    EXPECT_EQ(arr32[0], 0x03020100);

    kmemcpy(arr8 + 3, arr8 + 5, 4);
    EXPECT_EQ(arr32[0], 0x05020100);
    EXPECT_EQ(arr32[1], 0x07080706);
}

TEST(String, Length) {
    EXPECT_EQ(5, kstrlen("hello"));
    EXPECT_EQ(3, kstrlen("hel\0lo"));
}

// 符号函数，正数返回 1，负数返回 -1，零返回零
static int _sgn(int x) {
    return (x > 0) - (x < 0);
}

TEST(String, Compare) {
    const char s0[] = "test\0test";
    EXPECT_EQ(0, kstrcmp(s0, s0+5));

    const char s1[] = "hello";
    const char s2[] = "world";
    EXPECT_EQ(_sgn(kstrcmp(s1, s2)), _sgn(strcmp(s1, s2)));
    EXPECT_EQ(_sgn(kstrcmp(s2, s1)), _sgn(strcmp(s2, s1)));

    const char s3[] = "abc";
    const char s4[] = "abcde";
    EXPECT_EQ(_sgn(kstrcmp(s3, s4)), _sgn(strcmp(s3, s4)));
    EXPECT_EQ(_sgn(kstrcmp(s4, s3)), _sgn(strcmp(s4, s3)));
    EXPECT_EQ(_sgn(kmemcmp(s3, s4, 3)), _sgn(memcmp(s3, s4, 3)));
    EXPECT_EQ(_sgn(kmemcmp(s3, s4, 4)), _sgn(memcmp(s3, s4, 4)));
}

// // 字符串转换数字
// TEST(String, ToInt) {
//     EXPECT_EQ(123, strtou64("123"));
//     EXPECT_EQ(123, strtou64("123x"));
// }
