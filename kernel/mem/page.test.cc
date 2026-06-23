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

    void init(size_t start, size_t end) {
        page_init(start << PAGE_SHIFT, end << PAGE_SHIFT);
    }
    void add_free(size_t start, size_t end) {
        pages_add(start << PAGE_SHIFT, end << PAGE_SHIFT);
    }

    void validate_block(uint32_t blk, uint32_t end, int rank = -1) {
        EXPECT_EQ(g_pages[blk].head, 1);
        EXPECT_EQ(1U << g_pages[blk].rank, end - blk);
        if (rank >= 0) {
            EXPECT_EQ(g_pages[blk].rank, (uint32_t)rank);
        }
    }
};



// 物理地址边界检查
TEST_F(PageTest, CropHead) {
    init(0, 10);
    EXPECT_EQ(g_page_start, 1);
}
TEST_F(PageTest, CropTail) {
    init((size_t)UINT32_MAX - 5, (size_t)UINT32_MAX + 5);
    EXPECT_EQ(g_page_end, UINT32_MAX);
}
TEST_F(PageTest, NoMem) {
    EXPECT_EQ(g_pages, nullptr);
    init(8, 2);
    EXPECT_EQ(g_pages, nullptr);
}

TEST_F(PageTest, BuildBuddy) {
    init(1, 0x19000);

    add_free(8, 2); // 无效 add
    add_free(0, 0xff); // 收尾都不是 2 的幂

    validate_block(0x01, 0x02, 0);
    validate_block(0x02, 0x04, 1);
    validate_block(0x04, 0x08, 2);
    validate_block(0x08, 0x10, 3);
    validate_block(0x10, 0x20, 4);
    validate_block(0x20, 0x40, 5);
    validate_block(0x40, 0x80, 6);
    validate_block(0x80, 0xc0, 6);
    validate_block(0xc0, 0xe0, 5);
    validate_block(0xe0, 0xf0, 4);
    validate_block(0xf0, 0xf8, 3);
    validate_block(0xf8, 0xfc, 2);
    validate_block(0xfc, 0xfe, 1);
    validate_block(0xfe, 0xff, 0);

    // 和前面的 block 合并
    add_free(0xff, 0x800);
    validate_block(0x80,  0x100, 7);
    validate_block(0x100, 0x200, 8);
    validate_block(0x200, 0x400, 9);
    validate_block(0x400, 0x800, 10);

    // 后面的 block 不应超过最大 rank
    add_free(0x800, 0x19000);
    validate_block(0x800, 0x1000, 11);
    validate_block(0x1000, 0x2000, 12);
    validate_block(0x2000, 0x4000, 13);
    validate_block(0x4000, 0x8000, 14);
    validate_block(0x8000, 0x10000, 15);
    validate_block(0x10000, 0x18000, 15);
    validate_block(0x18000, 0x19000);
}

// max-rank=15，第一个超大块是 2^16 个页，即 0x10000 个页
// 如果遇到这种大的连续内存，rank 不能过界
TEST_F(PageTest, BuddyTooLarge) {
    init(0x10000, 0x20000);

    add_free(0x10000, 0x10100);
    add_free(0x10100, 0x10200);
    add_free(0x10200, 0x10300);
    add_free(0x10300, 0x10400);
    validate_block(0x10000, 0x10400, 10);

    add_free(0x10400, 0x10800);
    add_free(0x10800, 0x10c00);
    add_free(0x10c00, 0x11000);
    validate_block(0x10000, 0x11000, 12);

    add_free(0x11000, 0x12000);
    add_free(0x12000, 0x13000);
    add_free(0x13000, 0x14000);
    add_free(0x14000, 0x15000);
    add_free(0x15000, 0x16000);
    add_free(0x16000, 0x17000);
    add_free(0x17000, 0x18000);
    validate_block(0x10000, 0x18000, 15);

    add_free(0x18000, 0x1a000);
    add_free(0x1a000, 0x1c000);
    add_free(0x1c000, 0x1e000);
    add_free(0x1e000, 0x20000);
    validate_block(0x10000, 0x18000, 15);
    validate_block(0x18000, 0x20000, 15);
}

TEST_F(PageTest, Alloc) {
    init(1, 11);
    add_free(1, 11); // 一共只有 10 个页

    for (int i = 0; i < 10; ++i) {
        size_t pa = page_alloc(0, PT_FS);
        EXPECT_NE(pa, 0);
    }

    // 第 11 次分配应该失败
    EXPECT_EQ(page_alloc(0, PT_FS), 0);

    for (size_t i = 1; i < 11; ++i) {
        validate_block(i, i+1, 0);
        EXPECT_EQ(g_pages[i].type, PT_FS);
    }
}

TEST_F(PageTest, AllocSize) {
    init(1, 0x400);

    // 确保当前页都是不同大小的，rank=0~7
    // 每一种 rank 有 4 个块
    add_free(1,     0x100);
    add_free(0x101, 0x200);
    add_free(0x201, 0x300);
    add_free(0x301, 0x400);

    // 分配 rank-7，前四次成功，后一次失败
    for (int i = 0; i < 4; ++i) {
        EXPECT_NE(page_alloc(7, PT_FS), 0);
    }
    EXPECT_EQ(page_alloc(7, PT_FS), 0);

    // 分配 rank-5，前四次成功，后面八次拆分 rank-6 也可以成功，再分配则失败
    for (int i = 0; i < 4 + 8; ++i) {
        EXPECT_NE(page_alloc(5, PT_FS), 0);
    }
    EXPECT_EQ(page_alloc(5, PT_FS), 0);
}

TEST_F(PageTest, AllocColor) {
    init(1, 0x40);
    add_free(1, 0x40);

    // 要求起始地址是 n*0x10 + 5
    // 满足要求的页面总共有 4 个
    for (int i = 0; i < 4; ++i) {
        size_t pa = page_alloc_color(0, PT_FS, 0x10, 5);
        EXPECT_NE(pa, 0);
        EXPECT_EQ((pa >> PAGE_SHIFT) & 0x0f, 5);
    }
}

TEST_F(PageTest, AllocList) {
    init(1, 100);
    add_free(1, 100);

    pglist_t pl;
    pagelist_alloc(&pl, 63, PT_FS);

    uint32_t page_num = 0;
    for (uint32_t blk = pl.head; blk; blk = g_pages[blk].next) {
        page_num += 1U << g_pages[blk].rank;
    }

    pagelist_free(&pl);
    validate_block(1, 2);
    validate_block(2, 4);
    validate_block(4, 8);
    validate_block(8, 16);
    validate_block(16, 32);
    validate_block(32, 64);
    validate_block(64, 96);
    validate_block(96, 100);

    EXPECT_EQ(page_num, 63);
}

// 测试严重碎片化的情况
// 可以直接添加 2^n-1 个页，自然会分解成多种 rank
TEST_F(PageTest, AllocListFragmented) {
    init(1, 0x200);

    // 确保当前页都是不同大小的，每一种 rank 有两个块
    add_free(1, 32); // 1、2、4、8、16
    add_free(0x100, 0x200); // 添加一个远超过 32 的块

    // 63 = 1 + 2 + 4 + 8 + 16 + 32
    // 其中 1~16 都是现有的 block，最后一个 32-page-block 需要拆分
    pglist_t pgl;
    pagelist_alloc(&pgl, 63, PT_FS);

    uint32_t page_num = 0;
    for (uint32_t blk = pgl.head; blk; blk = g_pages[blk].next) {
        page_num += 1U << g_pages[blk].rank;
    }
    EXPECT_EQ(page_num, 63);
}
