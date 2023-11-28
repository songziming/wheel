#include <test.h>
#include <stdlib.h>

#include "../sources/page.c"



// 其他单元测试可能也用到了 early_alloc，应该将 arch_api 的实现放在
void *early_alloc_rw(size_t size) {
    return malloc(size);
}

void walk_pages() {
    pfn_t blk = 0;
    while (blk < g_page_num) {
        pfn_t size = block_size(blk);
        for (pfn_t i = 1; i < size; ++i) {
            EXPECT_TRUE(0 == g_pages[blk + i].head);
        }
        blk += size;
    }
    EXPECT_TRUE(blk == g_page_num);
}



static void setup() {
    page_init(1000);
}

static void teardown() {
    free(g_pages);
}


TEST_F(Page, Basic, setup, teardown) {
    //
}
