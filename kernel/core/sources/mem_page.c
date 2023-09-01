#include <mem_page.h>
#include <debug.h>


static CONST pfn_t g_page_num = 0;
static page_t *g_pages = NULL;


INIT_TEXT void pages_init(size_t start, size_t end) {
    ASSERT(0 == g_page_num);
    ASSERT(NULL == g_pages);
    ASSERT(start < end);

    start +=   PAGE_SIZE - 1;
    start &= ~(PAGE_SIZE - 1);
    end   &= ~(PAGE_SIZE - 1);

    // 分配页描述符
    g_page_num = (pfn_t)(end - start);
    g_pages = early_alloc_rw(g_page_num * sizeof(page_t));

    // 填充页描述符
    for (pfn_t i = 0; i < g_page_num; ++i) {
        g_pages[i].prev = INVALID_PFN;
        g_pages[i].next = INVALID_PFN;
    }
}
