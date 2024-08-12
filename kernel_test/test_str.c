#include "test.h"
#include <library/str.h>
#include <string.h>

// TODO 需要调用内核版本的 string，而不是标准库里面的

TEST(Str, Length) {
    EXPECT_TRUE(5 == def_strlen("hello"));
    EXPECT_TRUE(3 == def_strlen("hel\0lo"));
}

TEST(Str, Compare) {
    // EXPECT_TRUE(0 != def_strncmp("abc", "abcd", 5));
    // EXPECT_TRUE(0 != def_strncmp("abcd", "abc", 5));
    // EXPECT_TRUE(0 == def_strncmp("abcd", "abce", 3));
    // EXPECT_TRUE(0 != def_strncmp("abc", "abd", 3));
    EXPECT_TRUE(0 == def_memcmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != def_memcmp("abc", "abd", 3));
}

TEST(Str, Standard) {
    const char *s1 = "hello";
    const char *s2 = "world";
    // int d1 = def_strcmp(s1, s2);
    // int d2 = strcmp(s1, s2);
    // EXPECT_TRUE(d1 == d2, "%d %d", d1, d2);
    EXPECT_TRUE(def_strcmp(s1, s2) == strcmp(s1, s2));
}

// TEST(Str, Copy) {
//     char dst[10];
//     def_strncpy(dst, "abcde", 10);
//     for (int i = 5; i < 10; ++i) {
//         EXPECT_TRUE(0 == dst[i]);
//     }
// }
