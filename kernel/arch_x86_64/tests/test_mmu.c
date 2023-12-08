#include <test.h>
#include <stdint.h>
#include <stdlib.h>

#include <arch_config.h>
#include "test_mmu_helper.h"


// 必须保证分配的地址是页对齐的
// 除了分配一个页面，还要附带一个描述符
static size_t mock_page_alloc() {
    size_t addr = (size_t)aligned_alloc(PAGE_SIZE, PAGE_SIZE);
    mock_info_set(addr);
    return addr;
}

static void mock_page_free(size_t addr) {
    mock_info_clear(addr);
    free((void *)addr);
}

#define VIRT(pa)    (void *)(pa)
#define PAGE_ALLOC() mock_page_alloc()
#define PAGE_FREE(p) mock_page_free(p)
#define PAGE_INFO(p) mock_info_get(p)
#define SUPPORT_1G 1
#define SUPPORT_NX 1

#include "../sources/mem/mmu.c"
