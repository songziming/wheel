#include <gtest/gtest.h>

extern "C" {
#include "vmspace.h"
}


TEST(VmSpace, Sorted) {
    vmspace_t vm;
    dl_init_circular(&vm.head); // 不能使用标准初始化，否则会关联页表

    vmrange_t rng1 = { .addr = 0x1000, .end = 0x2000 };
    vmspace_insert(&vm, &rng1);

    vmrange_t rng3 = { .addr = 0x3000, .end = 0x4000 };
    vmspace_insert(&vm, &rng3);

    vmrange_t rng2 = { .addr = 0x2000, .end = 0x3000 };
    vmspace_insert(&vm, &rng2);

    EXPECT_EQ(rng1.dl.next, &rng2.dl);
    EXPECT_EQ(rng2.dl.prev, &rng1.dl);

    EXPECT_EQ(rng2.dl.next, &rng3.dl);
    EXPECT_EQ(rng3.dl.prev, &rng2.dl);
}
