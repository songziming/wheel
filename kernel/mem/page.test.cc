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

TEST_F(PageTest, Alloc) {
    page_init(1 << PAGE_SHIFT, 11 << PAGE_SHIFT);
    pages_add(1 << PAGE_SHIFT, 11 << PAGE_SHIFT);

    for (int i = 0; i < 10; ++i) {
        size_t pa = page_alloc(0, PT_KERNEL);
        EXPECT_NE(pa, 0);
    }

    // 第 11 次分配应该失败
    EXPECT_EQ(page_alloc(0, PT_KERNEL), 0);

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
        size_t pa = page_alloc_color(0, PT_KERNEL, 0x10, 7);
        EXPECT_NE(pa, 0);
    }
}
