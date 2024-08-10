#include "test.h"
#include <string.h>

// TODO 需要调用内核版本的 string，而不是标准库里面的

TEST(Str, Length) {
    EXPECT_TRUE(5 == kstrlen("hello", 20));
    EXPECT_TRUE(3 == kstrlen("hel\0lo", 20));
}

TEST(Str, Compare) {
    EXPECT_TRUE(0 != kstrncmp("abc", "abcd", 5));
    EXPECT_TRUE(0 != kstrncmp("abcd", "abc", 5));
    EXPECT_TRUE(0 == kstrncmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != kstrncmp("abc", "abd", 3));
    EXPECT_TRUE(0 == kmemcmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != kmemcmp("abc", "abd", 3));
}
