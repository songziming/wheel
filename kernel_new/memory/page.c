#include "page.h"
#include <arch_impl.h>
#include "early_alloc.h"


// 物理页类型
typedef enum page_type {
    PT_INVALID  = 0,    // 不存在或不可用
    PT_FREE,            // 未分配的可用内存
    PT_KERNEL,          // 被内核代码数据占用
    PT_PGTBL,           // 页表结构使用
    PT_KERNEL_STACK,    // 任务的内核栈
    PT_CACHE,           // 块设备缓存
} page_type_t;


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
