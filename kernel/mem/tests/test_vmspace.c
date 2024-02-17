#include <test.h>

#include "../sources/vmspace.c"

TEST(VmSpace, Sorted) {
    vmspace_t space;
    vmspace_init(&space);

    vmrange_t rng1;
    vmspace_insert(&space, &rng1, 0x1000, 0x2000, 0x1000, MMU_NONE, "1");

    vmrange_t rng3;
    vmspace_insert(&space, &rng3, 0x3000, 0x4000, 0x3000, MMU_NONE, "3");

    vmrange_t rng2;
    vmspace_insert(&space, &rng2, 0x2000, 0x3000, 0x2000, MMU_NONE, "2");

    EXPECT_TRUE(rng1.dl.next == &rng2.dl);
    EXPECT_TRUE(rng2.dl.prev == &rng1.dl);

    EXPECT_TRUE(rng2.dl.next == &rng3.dl);
    EXPECT_TRUE(rng3.dl.prev == &rng2.dl);
}
