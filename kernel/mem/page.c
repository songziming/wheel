#include "page.h"
#include "early_alloc.h"
#include <arch_api.h>
#include <kstring.h>
#include <debug.h>


CONST uint32_t g_page_start;
CONST uint32_t g_page_end;
CONST page_t *g_pages;

// 伙伴块一共有 16 个大小级别
#define RANK_NUM 16
static pglist_t g_blocks[RANK_NUM];


// 分配页描述符数组
INIT_TEXT void page_init(size_t pa_start, size_t pa_end) {
    pa_start >>= PAGE_SHIFT;
    pa_end += PAGE_SIZE - 1;
    pa_end >>= PAGE_SHIFT;

    // 0 地址必须是无效的
    if (pa_start == 0) {
        pa_start = 1;
    }
    if (pa_end > UINT32_MAX) {
        pa_end = UINT32_MAX;
    }
    if (pa_start >= pa_end) {
        logk("fatal: no usable physical memory\n");
        return;
    }

    g_page_start = (uint32_t)pa_start;
    g_page_end = (uint32_t)pa_end;
    uint32_t page_num = g_page_end - g_page_start;
    g_pages = early_alloc_rw(page_num * sizeof(page_t));
    g_pages -= g_page_start;

    kmemset(g_pages, 0, page_num * sizeof(page_t));
    kmemset(g_blocks, 0, sizeof(g_blocks));
    // for (uint32_t i = g_page_start; i < g_page_end; ++i) {
    //     g_pages[i].prev = 0;
    // }
}

INIT_TEXT void pages_add(size_t start, size_t end) {
    start +=   PAGE_SIZE - 1;
    start &= ~(PAGE_SIZE - 1);
    end   &= ~(PAGE_SIZE - 1);

    if (start < g_page_start) {
        start = g_page_start;
    }
    if (end > g_page_end) {
        end = g_page_end;
    }

    if (start < end) {
        logk("  + usable pages 0x%lx~0x%lx\n");
    }
}
