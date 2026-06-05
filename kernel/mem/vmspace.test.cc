#include <gtest/gtest.h>

extern "C" {
    #include "vmspace.h"
}

TEST(VmSpace, Add) {
    vmspace_t vm;
    vmrange_t rng1;
    vmrange_t rng2;
    vmrange_t rng3;

    vmspace_init(&vm, PAGE_SIZE, 4*PAGE_SIZE);

    EXPECT_TRUE(NULL == vmspace_alloc_nomap(&vm, &rng1, 4*PAGE_SIZE));   // 太大
    EXPECT_TRUE(NULL != vmspace_alloc_nomap(&vm, &rng1, PAGE_SIZE));
    EXPECT_TRUE(NULL != vmspace_alloc_nomap(&vm, &rng2, PAGE_SIZE));
    EXPECT_TRUE(NULL == vmspace_alloc_nomap(&vm, &rng3, PAGE_SIZE)); // 不足
}
