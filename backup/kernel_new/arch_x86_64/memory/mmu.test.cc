#include <gtest/gtest.h>
extern "C" {
#include <memory/page.h>
#include <arch_intf.h>
}

// 有必要拦截 page_alloc、page_info 两个函数
// mock 实现可以放在 page.test.cc


size_t page_block_alloc(int rank, page_type_t type) {
    return 0;
}

page_info_t *page_block_info(size_t addr) {
    return NULL;
}



TEST(MMU, Mapping) {
    //
}

