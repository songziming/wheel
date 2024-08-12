#include "test.h"
#include <str.h>
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

TEST(Str, Standard) {
    const char *s1 = "hello";
    const char *s2 = "world";
    // int d1 = kstrcmp(s1, s2);
    // int d2 = strcmp(s1, s2);
    // EXPECT_TRUE(d1 == d2, "%d %d", d1, d2);
    EXPECT_TRUE(kstrcmp(s1, s2) == strcmp(s1, s2));
}

TEST(Str, Copy) {
    char dst[10];
    kstrncpy(dst, "abcde", 10);
    for (int i = 5; i < 10; ++i) {
        EXPECT_TRUE(0 == dst[i]);
    }
}
