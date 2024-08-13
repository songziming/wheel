#include "test.h"
#include <string.h>
#include <library/string.h>


TEST(Str, Length) {
    EXPECT(5 == def_strlen("hello"));
    EXPECT(3 == def_strlen("hel\0lo"));
}

// TEST(Str, Compare) {
//     // EXPECT(0 != def_strncmp("abc", "abcd", 5));
//     // EXPECT(0 != def_strncmp("abcd", "abc", 5));
//     // EXPECT(0 == def_strncmp("abcd", "abce", 3));
//     // EXPECT(0 != def_strncmp("abc", "abd", 3));
//     EXPECT(0 == def_memcmp("abcd", "abce", 3));
//     EXPECT(0 != def_memcmp("abc", "abd", 3));
// }

// // 符号函数，正数返回 1，负数返回 -1，零返回零
// static int sgn(int x) {
//     return (x > 0) - (x < 0);
// }

// TEST(Str, Standard) {
//     const char *s1 = "hello";
//     const char *s2 = "world";
//     EXPECT(sgn(def_strcmp(s1, s2)) == sgn(strcmp(s1, s2)));
// }

// TEST(Str, Copy) {
//     char dst[10];
//     def_strncpy(dst, "abcde", 10);
//     for (int i = 5; i < 10; ++i) {
//         EXPECT(0 == dst[i]);
//     }
// }
