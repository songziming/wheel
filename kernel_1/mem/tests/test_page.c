#include <test.h>
#include <stdlib.h>

#include "../sources/page.c"


//------------------------------------------------------------------------------
// 检查状态正确性
//------------------------------------------------------------------------------

// 检查一个 block 是否合法
// 返回这个 block 的大小
pfn_t verify_block(pfn_t blk) {
    uint8_t rank = g_pages[blk].rank;
    pfn_t   size = 1U << rank;
    uint8_t type = g_pages[blk].type;

    // block 起始地址必须按 rank 对齐
    EXPECT_TRUE(0 == (blk & (size - 1)));

    // 块首页标记
    EXPECT_TRUE(0 != g_pages[blk].isblock);

    // 检查非起始页
    for (pfn_t i = 1; i < size; ++i) {
        EXPECT_TRUE(0 == g_pages[blk + i].isblock,
                "block %u+%u got isblock flag\n", blk, i);
        // EXPECT_TRUE(type == g_pages[blk + i].type);
    }

    // 如果这个 block 属于 FREE，需要确认属于 freelist
    if (PT_FREE == type) {
        pfn_t pn = g_free_blocks[rank].head;
        int found = 0;
        while (INVALID_PFN != pn) {
            if (blk == pn) {
                found = 1;
                break;
            }
            pn = g_pages[pn].next;
        }
        EXPECT_TRUE(found);
    }

    return size;
}

// 从头到尾检查一遍页描述符
void verify_all_pages() {
    for (pfn_t i = 0; i < g_page_count;) {
        EXPECT_TRUE(0 != g_pages[i].isblock);
        i += verify_block(i);
    }
}

// 检查所有的未分配块
void verify_all_free_blocks() {
    for (uint8_t rank = 0; rank < RANK_NUM; ++rank) {
        pfn_t size = 1U << rank;
        pfn_t blk  = g_free_blocks[rank].head;
        while (INVALID_PFN != blk) {
            EXPECT_TRUE(0 == (blk & (size - 1)));
            EXPECT_TRUE(rank == g_pages[blk].rank);
            EXPECT_TRUE(PT_FREE == g_pages[blk].type);
            blk = g_pages[blk].next;
        }
    }
}


//------------------------------------------------------------------------------
// 分配回收页面，同时检查结果
//------------------------------------------------------------------------------

pfn_t test_alloc(int rank) {
    pfn_t pn = page_block_alloc(rank, PT_STACK);

    pfn_t size = 1U << rank;
    EXPECT_TRUE(0 == (pn & (size - 1)));
    EXPECT_TRUE(pn + size <= g_page_count);

    return pn;
}

pfn_t test_alloc_phase(int rank, int period, int phase) {
    pfn_t pn = page_block_alloc_phase(rank, PT_STACK, period, phase);

    int size = 1 << rank;
    EXPECT_TRUE(0 == (pn & (size - 1)));
    EXPECT_TRUE(pn + size <= g_page_count);
    EXPECT_TRUE(phase == (pn & (period - 1)));

    return pn;
}

void test_free(pfn_t pn) {
    page_block_free(pn);

    // 回收之后，可能与相邻块合并
    EXPECT_TRUE((0 == g_pages[pn].isblock) || (PT_FREE == g_pages[pn].type));
}


//------------------------------------------------------------------------------
// 测试用例
//------------------------------------------------------------------------------

#define PAGE_COUNT 100
// #define COLOR_COUNT 16

static void setup() {
    pages_init(PAGE_COUNT);
    add_free_pages(0, PAGE_COUNT);
}

static void teardown() {
    free(g_pages);
}

TEST_F(Page, AllocFree, setup, teardown) {
    verify_all_pages();
    verify_all_free_blocks();

    pfn_t p1 = test_alloc(1);
    pfn_t p2 = test_alloc(2);
    pfn_t p3 = test_alloc(3);
    pfn_t p4 = test_alloc(4);
    pfn_t p5 = test_alloc(5);

    verify_all_pages();
    verify_all_free_blocks();

    test_free(p1);
    test_free(p2);
    test_free(p3);
    test_free(p4);
    test_free(p5);

    verify_all_pages();
    verify_all_free_blocks();
}

// // 整取零存
// TEST_F(Page, OneAllocMultiFree, setup, teardown) {
//     pfn_t pn  = test_alloc(5); // 32
//     for (int i = 0; i < 8; ++i) {
//         test_free(pn, 2); // 4
//         pn += 4;
//     }
// }

// 分配特定颜色的页面
TEST_F(Page, AllocColor, setup, teardown) {
    pfn_t p1 = test_alloc_phase(2, 16, 12);
    pfn_t p2 = test_alloc_phase(2, 16, 12);
    pfn_t p3 = test_alloc_phase(2, 16, 12);
    pfn_t p4 = test_alloc_phase(2, 16, 12);

    test_free(p1);
    test_free(p2);
    test_free(p3);
    test_free(p4);
}
