#ifndef PAGE_H
#define PAGE_H

#include <wheel.h>

// 页块类型
enum {
    PT_INVALID = 0,
    PT_FREE    = 1,
    PT_KERNEL  = 2,
    PT_PGTBL   = 3,
};

// 使用 uint32 表示页号，最多支持 4G-1 个物理页
typedef struct page {
    uint32_t prev;
    uint32_t next;

    uint32_t head : 1;   // 是不是块中第一个页
    uint32_t rank : 4;   // 所在块的大小，head==1 才有效
    uint32_t type : 4;   // 所在块的类型，head==1 才有效

    union {
        uint16_t    ent_num;    // page table 有效条目数量
    };
} page_t;

// 相同类型的块可以组成链表（双向不循环链表）
typedef struct pglist {
    uint32_t head;
    uint32_t tail;
} pglist_t;

extern uint32_t g_page_start;
extern uint32_t g_page_end;
extern page_t *g_pages;


size_t page_alloc_color(uint32_t rank, uint32_t type, uint32_t period, uint32_t phase);
size_t page_alloc(uint32_t rank, uint32_t type);
void page_free(size_t pa);
uint32_t page_free_count();

INIT_TEXT void page_init(size_t pa_start, size_t pa_end);
INIT_TEXT void pages_add(size_t start, size_t end);


#endif // PAGE_H