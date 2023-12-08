#include <test.h>
#include <cstring>

namespace os {
    #include "../sources/libk.c"
}


TEST(String, Len) {
    EXPECT_TRUE(5 == os::strlen("hello"));
    EXPECT_TRUE(3 == os::strlen("hel\0lo"));
    EXPECT_TRUE(3 == os::strnlen("hello", 3));
}

TEST(String, Copy) {
    char b[32];
    char *s;

    b[16] = 'a';
    b[17] = 'b';
    b[18] = 'c';
    b[19] =  0;

    EXPECT_TRUE(b == (s = os::strcpy(b, b + 16)));
    EXPECT_TRUE(0 == std::strcmp(s, "abc"));
    EXPECT_TRUE(b + 1 == (s = os::strcpy(b + 1, b + 16)));
    EXPECT_TRUE(0 == std::strcmp(s, "abc"));
    EXPECT_TRUE(b + 2 == (s = os::strcpy(b + 2, b + 16)));
    EXPECT_TRUE(0 == std::strcmp(s, "abc"));
    EXPECT_TRUE(b + 3 == (s = os::strcpy(b + 3, b + 16)));
    EXPECT_TRUE(0 == std::strcmp(s, "abc"));

    EXPECT_TRUE(b + 1 == (s = os::strcpy(b + 1, b + 17)));
    EXPECT_TRUE(0 == std::strcmp(s, "bc"));
    EXPECT_TRUE(b + 2 == (s = os::strcpy(b + 2, b + 18)));
    EXPECT_TRUE(0 == std::strcmp(s, "c"));
    EXPECT_TRUE(b + 3 == (s = os::strcpy(b + 3, b + 19)));
    EXPECT_TRUE(0 == std::strcmp(s, ""));

    EXPECT_TRUE(b == os::memset(b, 'x', sizeof(b)));
    EXPECT_TRUE(b == os::strncpy(b, "abc", sizeof(b) - 1)); // 应该将剩余部分填零
    EXPECT_TRUE(0 == os::memcmp(b, "abc\0\0\0\0", 8));
    EXPECT_TRUE('x' == b[sizeof(b) - 1]);

    b[3] = 'x';
    b[4] =  0;
    os::strncpy(b, "abc", 3);
    EXPECT_TRUE('c' == b[2]);
    EXPECT_TRUE('x' == b[3]);

    os::memcpy(b, "abcde", 5);
    EXPECT_TRUE(0 == os::strcmp(b, "abcde"));
    os::memmove(&b[10], b, 5);
    EXPECT_TRUE(0 == os::strcmp(&b[10], "abcde"));
    os::memmove(&b[1], b, 5);
    EXPECT_TRUE(0 == os::strcmp(&b[1], "abcde"));
}


TEST(String, Compare) {
    EXPECT_TRUE(0 == os::strncmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != os::strncmp("abc", "abd", 3));
    EXPECT_TRUE(0 == os::strcmp("abc", "abc"));
    EXPECT_TRUE(0 != os::strcmp("abc", "abd"));
    EXPECT_TRUE(0 == os::memcmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != os::memcmp("abc", "abd", 3));
}
