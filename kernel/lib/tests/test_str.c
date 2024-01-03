#include <test.h>

#include "../sources/str.c"

TEST(Str, Length) {
    EXPECT_TRUE(5 == kstrlen("hello", 20));
    EXPECT_TRUE(3 == kstrlen("hel\0lo", 20));
    EXPECT_TRUE(3 == kstrlen("hello", 3));
}

TEST(Str, Compare) {
    EXPECT_TRUE(0 != kstrcmp("abc", "abcd", 5));
    EXPECT_TRUE(0 != kstrcmp("abcd", "abc", 5));
    EXPECT_TRUE(0 == kstrcmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != kstrcmp("abc", "abd", 3));
    EXPECT_TRUE(0 == kmemcmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != kmemcmp("abc", "abd", 3));
}
