#include <test.h>
#include <stdlib.h>

#include "../sources/page.c"



// 其他单元测试可能也用到了 early_alloc，应该将 arch_api 的实现放在
void *early_alloc_rw(size_t size) {
    return malloc(size);
}


static void setup() {
    page_init(1000 << PAGE_SHIFT);
}

static void teardown() {
    free(g_pages);
    g_pages = NULL;
    g_page_num = 0;
}


// 检查块结构是否正常
void walk_pages() {
    pfn_t blk = 0;
    while (blk < g_page_num) {
        pfn_t size = page_block_size(blk);
        for (pfn_t i = 1; i < size; ++i) {
            EXPECT_TRUE(0 == g_pages[blk + i].head);
        }
        blk += size;
    }
    EXPECT_TRUE(blk == g_page_num);
}


TEST_F(Page, BuildBlocks, setup, teardown) {
    walk_pages();
    page_add(10 << PAGE_SHIFT, 20 << PAGE_SHIFT, PT_FREE);
    page_add(30 << PAGE_SHIFT, 40 << PAGE_SHIFT, PT_KERNEL);
    page_add(50 << PAGE_SHIFT, 60 << PAGE_SHIFT, PT_FREE);
    walk_pages();
    page_add(20 << PAGE_SHIFT, 30 << PAGE_SHIFT, PT_FREE);
    page_add(40 << PAGE_SHIFT, 50 << PAGE_SHIFT, PT_KERNEL);
    page_add(60 << PAGE_SHIFT, 70 << PAGE_SHIFT, PT_FREE);
    walk_pages();
}

TEST_F(Page, AllocPages, setup, teardown) {
    page_add(10 << PAGE_SHIFT, 20 << PAGE_SHIFT, PT_FREE);

    int num = 0;
    while (1) {
        size_t pa = pages_alloc(PT_KERNEL);
        if (INVALID_ADDR == pa) {
            break;
        }
        ++num;
    }

    EXPECT_TRUE(10 == num);
}

