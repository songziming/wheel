#include <test.h>

#include "../sources/strlib.c"

TEST(Str, Length) {
    EXPECT_TRUE(5 == slen("hello", 20));
    EXPECT_TRUE(3 == slen("hel\0lo", 20));
    EXPECT_TRUE(3 == slen("hello", 3));
}

TEST(Str, Compare) {
    EXPECT_TRUE(0 == scmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != scmp("abc", "abd", 3));
    EXPECT_TRUE(0 == bcmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != bcmp("abc", "abd", 3));
}
