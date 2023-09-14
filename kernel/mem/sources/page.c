#include <page.h>
#include <debug.h>


static CONST pfn_t g_page_start = 0;
static CONST pfn_t g_page_num = 0;
static CONST page_t *g_pages = NULL;


// 启动阶段分配页描述符数组
INIT_TEXT void pages_init(size_t start, size_t end) {
    ASSERT(0 == g_page_num);
    ASSERT(NULL == g_pages);
    ASSERT(start < end);

    start  += PAGE_SIZE - 1;
    start >>= PAGE_SHIFT;
    end   >>= PAGE_SHIFT;

    if (end > INVALID_PFN) {
        end = INVALID_PFN;
    }

    // 分配页描述符
    g_page_start = (pfn_t)start;
    g_page_num = (pfn_t)(end - start);
    g_pages = early_alloc_rw(g_page_num * sizeof(page_t));

    // 填充页描述符
    for (pfn_t i = 0; i < g_page_num; ++i) {
        g_pages[i].prev = INVALID_PFN;
        g_pages[i].next = INVALID_PFN;
    }

    // 这样，页描述符数组就可以用常规页号访问
    g_pages -= g_page_start;
}

INIT_TEXT void pages_add(pfn_t start, pfn_t end) {
    ASSERT(0 != g_page_num);
    ASSERT(NULL != g_pages);
    ASSERT(start >= g_page_start);
    ASSERT(end >= g_page_start);

    if (start == end) {
        return;
    }

    for (pfn_t i = start; i <= end; ++i) {
        g_pages[i].prev = i;
        g_pages[i].next = i;
    }

    klog(">>> adding page range %x~%x\n", start, end);
}
