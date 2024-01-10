#include <test.h>

#include "../sources/str.c"

TEST(Str, Length) {
    EXPECT_TRUE(5 == kstrnlen("hello", 20));
    EXPECT_TRUE(3 == kstrnlen("hel\0lo", 20));
    EXPECT_TRUE(3 == kstrnlen("hello", 3));
}

TEST(Str, Compare) {
    EXPECT_TRUE(0 != kstrncmp("abc", "abcd", 5));
    EXPECT_TRUE(0 != kstrncmp("abcd", "abc", 5));
    EXPECT_TRUE(0 == kstrncmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != kstrncmp("abc", "abd", 3));
    EXPECT_TRUE(0 == kmemcmp("abcd", "abce", 3));
    EXPECT_TRUE(0 != kmemcmp("abc", "abd", 3));
}


#if 0

TEST(Mem, Copy) {
    fprintf(stdout, "testing memcpy\n");

    char dst[1024];
    char src[] = "hello, world";

    fprintf(stdout, "vanilla string:\n");
    memcpy(dst, src, 10);
    fprintf(stdout, "custom string:\n");
    kmemcpy(dst, src, 10);
}

TEST(Mem, Move) {
    char dst[1024];
    char src[] = "hello, world";

    fprintf(stdout, "vanilla memmove:\n");
    memmove(dst, src, 10);

    fprintf(stdout, "kernel memmove:\n");
    kmemmove(dst, src, 10);
}

#endif
