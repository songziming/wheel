#include <test.h>
#include <stdio.h>
#include <stdlib.h>

#include <arch_config.h>
#include <page.h>
#include "test_mmu_helper.h"


// 必须保证分配的地址是页对齐的
// 除了分配一个页面，还要分配一个描述符
static size_t mock_pages_alloc() {
    size_t addr = (size_t)aligned_alloc(PAGE_SIZE, PAGE_SIZE);
    page_info_t *info = (page_info_t *)malloc(sizeof(page_info_t));
    info->ent_num = 999;
    mock_info_set(addr, info);
    // printf("mock alloc page %lx\n", addr);
    return addr;
}

static void mock_pages_free(size_t addr) {
    // printf("mock free page %lx\n", addr);
    free((void *)addr);
    free(mock_info_get(addr));
    mock_info_clear(addr);
}

#define VIRT(pa) (void *)(pa)
#define PAGE_ALLOC() mock_pages_alloc()
#define PAGE_FREE(p) mock_pages_free(p)
#define PAGE_INFO(p) mock_info_get(p)
#define SUPPORT_1G 1
#define SUPPORT_NX 1

// static void mmu_show_map(uint64_t va, uint64_t pa, uint64_t size, uint64_t attr, int nitems) {
//     printf("%lx:%lx --> %lx,")
// }

// #define SHOW_MAP

#include "../sources/mem/mmu.c"


#define K  1024L
#define M (1024L*K)
#define G (1024L*M)


void klog(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

TEST(MMU, Build) {
    size_t tbl = alloc_table();
    mmu_table_delete(tbl);
}

TEST(MMU, Map) {
    size_t tbl = alloc_table();

    mmu_map(tbl, 2*G, 3*G, 1*G, MMU_NONE);  // 2G~3G --> 1G
    mmu_map(tbl, 4*M, 6*M, 2*M, MMU_NONE);  // 4M~6M --> 2M
    mmu_map(tbl, 4*K, 8*K, 8*K, MMU_NONE);  // 4K~8K --> 8K

    mmu_attr_t attrs;
    EXPECT_TRUE(mmu_translate(tbl, 2*G + 100, &attrs) == 1*G + 100);
    EXPECT_TRUE(mmu_translate(tbl, 5*M + 100, &attrs) == 3*M + 100);
    EXPECT_TRUE(mmu_translate(tbl, 5*K + 100, &attrs) == 9*K + 100);

    // 重新 map，拆分原有大页
    mmu_map(tbl, 2*G+8*M, 3*G-8*M, 8*M, MMU_NONE);
    EXPECT_TRUE(mmu_translate(tbl, 2*G+10*M, &attrs) == 10 *M);
    EXPECT_TRUE(mmu_translate(tbl, 2*G+6*M, &attrs) == 1*G+6*M); // 开头剩余
    EXPECT_TRUE(mmu_translate(tbl, 3*G-6*M, &attrs) == 2*G-6*M); // 结尾剩余

    mmu_table_delete(tbl);
}
