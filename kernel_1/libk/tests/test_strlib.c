#include <test.h>
#include <string.h>

#include "../sources/strlib.c"


TEST(String, Len) {
    EXPECT_TRUE(5 == slen("hello", 20));
    EXPECT_TRUE(3 == slen("hel\0lo", 20));
    EXPECT_TRUE(3 == slen("hello", 3));
}

TEST(String, Copy) {
    char b[32];
    char *s;

    b[16] = 'a';
    b[17] = 'b';
    b[18] = 'c';
    b[19] =  0;

    EXPECT_TRUE(b == (s = scopy(b, b + 16, 16)));
    EXPECT_TRUE(0 == strcmp(s, "abc"));
    EXPECT_TRUE(b + 1 == (s = scopy(b + 1, b + 16, 15)));
    EXPECT_TRUE(0 == strcmp(s, "abc"));
    EXPECT_TRUE(b + 2 == (s = scopy(b + 2, b + 16, 14)));
    EXPECT_TRUE(0 == strcmp(s, "abc"));
    EXPECT_TRUE(b + 3 == (s = scopy(b + 3, b + 16, 13)));
    EXPECT_TRUE(0 == strcmp(s, "abc"));

    EXPECT_TRUE(b + 1 == (s = scopy(b + 1, b + 17, 15)));
    EXPECT_TRUE(0 == strcmp(s, "bc"));
    EXPECT_TRUE(b + 2 == (s = scopy(b + 2, b + 18, 14)));
    EXPECT_TRUE(0 == strcmp(s, "c"));
    EXPECT_TRUE(b + 3 == (s = scopy(b + 3, b + 19, 13)));
    EXPECT_TRUE(0 == strcmp(s, ""));

    EXPECT_TRUE(b == mfill(b, 'x', sizeof(b)));
    EXPECT_TRUE(b == scopy(b, "abc", sizeof(b) - 1)); // 应该将剩余部分填零
    EXPECT_TRUE(0 == mdiff(b, "abc\0\0\0\0", 8));
    EXPECT_TRUE('x' == b[sizeof(b) - 1]);

    b[3] = 'x';
    b[4] =  0;
    scopy(b, "abc", 3);
    EXPECT_TRUE('c' == b[2]);
    EXPECT_TRUE('x' == b[3]);

    mcopy(b, "abcde", 5);
    EXPECT_TRUE(0 == strcmp(b, "abcde"));
    mmove(&b[10], b, 5);
    EXPECT_TRUE(0 == strcmp(&b[10], "abcde"));
    mmove(&b[1], b, 5);
    EXPECT_TRUE(0 == strcmp(&b[1], "abcde"));
}


TEST(String, Compare) {
    EXPECT_TRUE(0 == sdiff("abcd", "abce", 3));
    EXPECT_TRUE(0 != sdiff("abc", "abd", 3));
    // EXPECT_TRUE(0 == kstrcmp("abc", "abc"));
    // EXPECT_TRUE(0 != kstrcmp("abc", "abd"));
    EXPECT_TRUE(0 == mdiff("abcd", "abce", 3));
    EXPECT_TRUE(0 != mdiff("abc", "abd", 3));
}
