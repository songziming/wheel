#include <test.h>

#include "../sources/vmspace.c"


TEST(VMSpace, FindInEmpty) {
    vmspace_t space;
    vmspace_init(&space);

    size_t va1 = _find_unused(&space, 0x1000, 0x2000, 0x4000, 1);
    size_t va2 = _find_unused(&space, 0x2000, 0x3000, 0x4000, 1);

    EXPECT_TRUE(0x2000 == va1);
    EXPECT_TRUE((size_t)-1 == va2);
}

TEST(VMSpace, FindInSingleRange) {
    vmspace_t space;
    vmspace_init(&space);

    // 0x5000...0x9000
    vmrange_t range = {
        .va   = 0x5000,
        .size = 0x4000,
    };
    vmspace_insert_range(&space, &range);

    size_t va1 = _find_unused(&space, 0x1000, 0x3000, 0x6000, 1);
    size_t va2 = _find_unused(&space, 0x1000, 0x6000, 0x8000, 1);
    size_t va3 = _find_unused(&space, 0x1000, 0x8000, 0xb000, 1);
    size_t va4 = _find_unused(&space, 0x2000, 0x4000, 0xf000, 1);

    EXPECT_TRUE(0x3000 == va1);
    EXPECT_TRUE((size_t)-1 == va2);
    EXPECT_TRUE(0x9000 == va3);
    EXPECT_TRUE(0x9000 == va4);
}
