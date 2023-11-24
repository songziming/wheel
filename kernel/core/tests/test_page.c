#include <test.h>
#include <stdlib.h>

#include "../sources/page.c"



// 其他单元测试可能也用到了 early_alloc，应该将 arch_api 的实现放在
void *early_alloc_rw(size_t size) {
    return malloc(size);
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
