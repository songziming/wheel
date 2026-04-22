#include <gtest/gtest.h>
#include "early_alloc.mock.h"

extern "C" {
    #include "page.h"
    #include <arch_api.h>
}


class PageTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_pages = nullptr;
    }
    void TearDown() override {
        clear_early_chunks();
    }
};




TEST_F(PageTest, CropHead) {
    page_init(0, 10 * PAGE_SIZE);
    EXPECT_EQ(g_page_start, 1);
}

TEST_F(PageTest, CropTail) {
    page_init(((size_t)UINT32_MAX - 5) << PAGE_SHIFT, ((size_t)UINT32_MAX + 5) << PAGE_SHIFT);
    EXPECT_EQ(g_page_end, UINT32_MAX);
}

TEST_F(PageTest, NoMem) {
    EXPECT_EQ(g_pages, nullptr);
    page_init(8 << PAGE_SHIFT, 2 << PAGE_SHIFT);
    EXPECT_EQ(g_pages, nullptr);
}

#define CHECK_BLOCK(b, r) do {      \
    EXPECT_EQ(g_pages[b].head, 1);  \
    EXPECT_EQ(g_pages[b].rank, r);  \
} while (0)

TEST_F(PageTest, BuildBuddy) {
    page_init(1 << PAGE_SHIFT, 0x18000 << PAGE_SHIFT);

    pages_add(8 << PAGE_SHIFT, 2 << PAGE_SHIFT); // 无效 add
    pages_add(0 << PAGE_SHIFT, 256 << PAGE_SHIFT);
    pages_add(256 << PAGE_SHIFT, 0x9000 << PAGE_SHIFT);
    pages_add(0x9000 << PAGE_SHIFT, 0x19000 << PAGE_SHIFT);

    CHECK_BLOCK(1, 0);
    CHECK_BLOCK(2, 1);
    CHECK_BLOCK(4, 2);
    CHECK_BLOCK(8, 3);

    CHECK_BLOCK(0x10, 4);
    CHECK_BLOCK(0x20, 5);
    CHECK_BLOCK(0x40, 6);
    CHECK_BLOCK(0x80, 7);

    CHECK_BLOCK(0x100, 8);
    CHECK_BLOCK(0x200, 9);
    CHECK_BLOCK(0x400, 10);
    CHECK_BLOCK(0x800, 11);

    CHECK_BLOCK(0x1000, 12);
    CHECK_BLOCK(0x2000, 13);
    CHECK_BLOCK(0x4000, 14);
    CHECK_BLOCK(0x8000, 15);
    CHECK_BLOCK(0x10000, 15);
}

// max-rank=15，第一个超大块是 2^16 个页，即 0x10000 个页
// 如果遇到这种大的连续内存，rank 不能过界
TEST_F(PageTest, BuddyTooLarge) {
    page_init(0x10000 << PAGE_SHIFT, 0x20000 << PAGE_SHIFT);

    pages_add(0x10000 << PAGE_SHIFT, 0x10100 << PAGE_SHIFT);
    pages_add(0x10100 << PAGE_SHIFT, 0x10200 << PAGE_SHIFT);
    pages_add(0x10200 << PAGE_SHIFT, 0x10300 << PAGE_SHIFT);
    pages_add(0x10300 << PAGE_SHIFT, 0x10400 << PAGE_SHIFT);
    CHECK_BLOCK(0x10000, 10); // 0x10000~0x10400, rank 10

    pages_add(0x10400 << PAGE_SHIFT, 0x10800 << PAGE_SHIFT);
    pages_add(0x10800 << PAGE_SHIFT, 0x10c00 << PAGE_SHIFT);
    pages_add(0x10c00 << PAGE_SHIFT, 0x11000 << PAGE_SHIFT);
    CHECK_BLOCK(0x10000, 12); // 0x10000~0x11000, rank 12

    pages_add(0x11000 << PAGE_SHIFT, 0x12000 << PAGE_SHIFT);
    pages_add(0x12000 << PAGE_SHIFT, 0x13000 << PAGE_SHIFT);
    pages_add(0x13000 << PAGE_SHIFT, 0x14000 << PAGE_SHIFT);
    pages_add(0x14000 << PAGE_SHIFT, 0x15000 << PAGE_SHIFT);
    pages_add(0x15000 << PAGE_SHIFT, 0x16000 << PAGE_SHIFT);
    pages_add(0x16000 << PAGE_SHIFT, 0x17000 << PAGE_SHIFT);
    pages_add(0x17000 << PAGE_SHIFT, 0x18000 << PAGE_SHIFT);
    CHECK_BLOCK(0x10000, 15); // 0x10000~0x18000, rank 15

    pages_add(0x18000 << PAGE_SHIFT, 0x1a000 << PAGE_SHIFT);
    pages_add(0x1a000 << PAGE_SHIFT, 0x1c000 << PAGE_SHIFT);
    pages_add(0x1c000 << PAGE_SHIFT, 0x1e000 << PAGE_SHIFT);
    pages_add(0x1e000 << PAGE_SHIFT, 0x20000 << PAGE_SHIFT);
    CHECK_BLOCK(0x10000, 15); // 0x10000~0x18000, rank 15
    CHECK_BLOCK(0x18000, 15); // 0x18000~0x20000, rank 15
}

TEST_F(PageTest, Alloc) {
    page_init(1 << PAGE_SHIFT, 11 << PAGE_SHIFT);
    pages_add(1 << PAGE_SHIFT, 11 << PAGE_SHIFT);

    for (int i = 0; i < 10; ++i) {
        size_t pa = PAGE_ALLOC(0, PT_KERNEL);
        EXPECT_NE(pa, 0);
    }

    // 第 11 次分配应该失败
    EXPECT_EQ(PAGE_ALLOC(0, PT_KERNEL), 0);

    for (size_t i = 1; i < 11; ++i) {
        CHECK_BLOCK(i, 0);
        EXPECT_EQ(g_pages[i].type, PT_KERNEL);
    }
}

TEST_F(PageTest, AllocColor) {
    page_init(1 << PAGE_SHIFT, 0x40 << PAGE_SHIFT);
    pages_add(1 << PAGE_SHIFT, 0x40 << PAGE_SHIFT);

    // 满足要求的页面总共有 4 个
    for (int i = 0; i < 4; ++i) {
        size_t pa = page_alloc_color(0, PT_KERNEL, 0x10, 7, __FILE__, __LINE__);
        EXPECT_NE(pa, 0);
    }
}
