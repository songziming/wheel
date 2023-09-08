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
#ifdef DEBUG
    dbg_print("allocating page descriptors for %zx~%zx\n", start, end);
#endif
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
}
