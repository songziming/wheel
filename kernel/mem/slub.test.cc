#include <gtest/gtest.h>
#include <sys/mman.h>
#include <early_alloc.mock.h>

extern "C" {
    #include "slub.h"
    #include <arch_config.h>
}

extern uint64_t g_direct_map_base;


// SLUB 依赖物理内存分配，使用 mmap 申请一段内存模拟物理内存
// TODO SLUB 不像 mmu 依赖明确的物理地址，完全可以通过 malloc 申请一个 slab
class SlubTest : public ::testing::Test {
    const size_t npages = 256;
    void *va_ = nullptr;

protected:
    void SetUp() override {
        int prot = PROT_READ | PROT_WRITE;
        int flags = MAP_PRIVATE | MAP_ANONYMOUS;
        va_ = mmap(nullptr, npages << PAGE_SHIFT, prot, flags, -1, 0);
        ASSERT_NE(va_, MAP_FAILED);
        g_direct_map_base = (uint64_t)va_ - PAGE_SIZE;

        page_init(PAGE_SIZE, (npages + 1) << PAGE_SHIFT);
        pages_add(PAGE_SIZE, (npages + 1) << PAGE_SHIFT);
    }

    void TearDown() override {
        munmap(va_, npages << PAGE_SHIFT);
        clear_early_chunks();
    }
};

TEST_F(SlubTest, AllocAndFree) {
    slub_t slub;
    slub_init(&slub, 64);

    // 分配 8 个对象
    void *objs[8];
    for (int i = 0; i < 8; ++i) {
        objs[i] = slub_alloc(&slub);
        ASSERT_NE(objs[i], nullptr);
        // 写入测试模式
        *(uint64_t *)objs[i] = 0xDEADBEEF00000000UL + i;
    }

    // 8 个对象应该互不相同
    for (int i = 0; i < 8; ++i) {
        for (int j = i + 1; j < 8; ++j) {
            EXPECT_NE(objs[i], objs[j]);
        }
    }

    // 释放中间 4 个
    for (int i = 2; i < 6; ++i) {
        slub_free(&slub, objs[i]);
    }

    // 再分配 4 个
    void *new_objs[4];
    for (int i = 0; i < 4; ++i) {
        new_objs[i] = slub_alloc(&slub);
        ASSERT_NE(new_objs[i], nullptr);
        *(uint64_t *)new_objs[i] = 0xCAFE000000000000UL + i;
    }

    // 验证未释放的 4 个对象数据未损坏
    EXPECT_EQ(*(uint64_t *)objs[0], 0xDEADBEEF00000000UL + 0);
    EXPECT_EQ(*(uint64_t *)objs[1], 0xDEADBEEF00000000UL + 1);
    EXPECT_EQ(*(uint64_t *)objs[6], 0xDEADBEEF00000000UL + 6);
    EXPECT_EQ(*(uint64_t *)objs[7], 0xDEADBEEF00000000UL + 7);

    slub_destroy(&slub);
}
