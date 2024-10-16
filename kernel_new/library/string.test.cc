#include <gtest/gtest.h>
#include <string.h>

extern "C" {
#include <library/string.h>
}


TEST(String, Length) {
    EXPECT_EQ(5, def_strlen("hello"));
    EXPECT_EQ(3, def_strlen("hel\0lo"));
}

// 符号函数，正数返回 1，负数返回 -1，零返回零
static int sgn(int x) {
    return (x > 0) - (x < 0);
}

TEST(String, Compare) {
    const char *s1 = "hello";
    const char *s2 = "world";
    EXPECT_EQ(sgn(def_strcmp(s1, s2)), sgn(strcmp(s1, s2)));
    EXPECT_EQ(sgn(def_strcmp(s2, s1)), sgn(strcmp(s2, s1)));

    const char *s3 = "abc";
    const char *s4 = "abcde";
    EXPECT_EQ(sgn(def_strcmp(s3, s4)), sgn(strcmp(s3, s4)));
    EXPECT_EQ(sgn(def_strcmp(s4, s3)), sgn(strcmp(s4, s3)));
}
