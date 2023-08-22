#include <test.h>
#include <string.h>

#include "../sources/lib_string.c"


TEST(String, Len) {
    EXPECT_TRUE(5 == kstrlen("hello"));
    EXPECT_TRUE(3 == kstrlen("hel\0lo"));
    EXPECT_TRUE(3 == kstrnlen("hello", 3));
}

TEST(String, Copy) {
    char b[32];
    char *s;

    b[16] = 'a';
    b[17] = 'b';
    b[18] = 'c';
    b[19] =  0;

    EXPECT_TRUE(b == (s = kstrcpy(b, b + 16)));
    EXPECT_TRUE(0 == strcmp(s, "abc"));
    EXPECT_TRUE(b + 1 == (s = kstrcpy(b + 1, b + 16)));
    EXPECT_TRUE(0 == strcmp(s, "abc"));
    EXPECT_TRUE(b + 2 == (s = kstrcpy(b + 2, b + 16)));
    EXPECT_TRUE(0 == strcmp(s, "abc"));
    EXPECT_TRUE(b + 3 == (s = kstrcpy(b + 3, b + 16)));
    EXPECT_TRUE(0 == strcmp(s, "abc"));

    EXPECT_TRUE(b + 1 == (s = kstrcpy(b + 1, b + 17)));
    EXPECT_TRUE(0 == strcmp(s, "bc"));
    EXPECT_TRUE(b + 2 == (s = kstrcpy(b + 2, b + 18)));
    EXPECT_TRUE(0 == strcmp(s, "c"));
    EXPECT_TRUE(b + 3 == (s = kstrcpy(b + 3, b + 19)));
    EXPECT_TRUE(0 == strcmp(s, ""));

    EXPECT_TRUE(b == kmemset(b, 'x', sizeof(b)));
    EXPECT_TRUE(b == kstrncpy(b, "abc", sizeof(b) - 1)); // 应该将剩余部分填零
    EXPECT_TRUE(0 == kmemcmp(b, "abc\0\0\0\0", 8));
    EXPECT_TRUE('x' == b[sizeof(b) - 1]);

    b[3] = 'x';
    b[4] =  0;
    kstrncpy(b, "abc", 3);
    EXPECT_TRUE('c' == b[2]);
    EXPECT_TRUE('x' == b[3]);

    kmemcpy(b, "abcde", 5);
    EXPECT_TRUE(0 == kstrcmp(b, "abcde"));
    kmemmove(&b[10], b, 5);
    EXPECT_TRUE(0 == kstrcmp(&b[10], "abcde"));
    kmemmove(&b[1], b, 5);
    EXPECT_TRUE(0 == kstrcmp(&b[1], "abcde"));
}


TEST(String, Compare) {
    EXPECT_TRUE(0 == kstrncmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != kstrncmp("abc", "abd", 3));
    EXPECT_TRUE(0 == kstrcmp("abc", "abc"));
    EXPECT_TRUE(0 != kstrcmp("abc", "abd"));
    EXPECT_TRUE(0 == kmemcmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != kmemcmp("abc", "abd", 3));
}
