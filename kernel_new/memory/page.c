#include "page.h"
#include <arch_impl.h>
#include "early_alloc.h"
#include <library/debug.h>



typedef struct page {
    pfn_t prev;
    pfn_t next;

    uint32_t head : 1;   // 是不是块中第一个页
    uint32_t rank : 4;   // 所在块的大小，head==1 才有效
    uint32_t type : 4;   // 所在块的类型，head==1 才有效
} page_t;




static CONST pfn_t g_page_num = 0;
static page_t *g_pages = NULL;




INIT_TEXT void page_init(size_t end) {
    ASSERT(0 == g_page_num);
    ASSERT(NULL == g_pages);

    end += PAGE_SIZE - 1;
    end >>= PAGE_SHIFT;

    if (end > INVALID_PFN) {
        end = INVALID_PFN;
    }

    g_page_num = (pfn_t)end;
    g_pages = early_alloc_rw(end * sizeof(page_t));

    for (pfn_t i = 0; i < g_page_num; ++i) {
        g_pages[i].prev = INVALID_PFN;
        g_pages[i].next = INVALID_PFN;
        g_pages[i].type = PT_INVALID;
        g_pages[i].head = 1;
        g_pages[i].rank = 0;
    }
}

INIT_TEXT void page_set_type(pfn_t start, pfn_t end, uint8_t type) {
    ASSERT(0 != g_page_num);
    ASSERT(NULL != g_pages);

    if (end > g_page_num) {
        end = g_page_num;
    }

    // TODO 应该把连续相同类型的 page 合并为尽可能大的 block
    for (pfn_t i = start; i < end; ++i) {
        g_pages[i].type = type;
    }
}

// 回收若干连续物理页
static void page_range_free(pfn_t start, int n) {
    ASSERT(start + n <= g_page_num);
    ASSERT(NULL != g_pages);

    for (int i = 0; i < n; ++i) {
        g_pages[start + i].type = PT_FREE;
    }
}

void pages_free(size_t start, size_t end) {
    start >>= PAGE_SHIFT;
    end += PAGE_SIZE - 1;
    end >>= PAGE_SHIFT;
    page_range_free(start, end - start);
}
