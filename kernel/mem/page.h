#ifndef PAGE_H
#define PAGE_H

#include <wheel.h>

// 页块类型
typedef enum page_type {
    PT_INVALID = 0,
    PT_FREE    = 1,
    PT_KERNEL  = 2,
    PT_PGTBL   = 3,
    PT_STACK   = 4,
    PT_POOL    = 5,
    PT_MSGQ    = 6,
    PT_FS      = 7,
} page_type_t;

// rank 合法取值 0~15
#define PAGE_BLOCK_RANK_NUM 16

// 使用 uint32 表示页号，最多支持 4G-1 个物理页
typedef struct page {
    uint32_t prev;
    uint32_t next;

    uint32_t head : 1;  // 是不是块中第一个页
    uint32_t rank : 4;  // 所在块的大小，head==1 才有效
    uint32_t type : 4;  // 所在块的类型，head==1 才有效

    // 对于 PT_PGTBL，表示页表中有效条目数量
    // 对于 PT_POOL，表示已使用的 object 数量（inuse）
    uint32_t ent_num : 16;

    // 对于 PT_POOL，表示 freelist 头（slab 内对象偏移，0xFFFF 表示空）
    uint32_t objects : 16;

#if DEBUG
    const char *file;
    int         line;
#endif
} page_t;

// 相同类型的块可以组成链表（双向不循环链表）
typedef struct pglist {
    uint32_t head;
    uint32_t tail;
} pglist_t;

extern uint32_t g_page_start;
extern uint32_t g_page_end;
extern page_t *g_pages;

// 找出所在块
uint32_t page_block_head(uint32_t pfn);

// 页链表操作
void pglist_push_tail(pglist_t *pl, uint32_t blk);
void pglist_push_head(pglist_t *pl, uint32_t blk);
void pglist_remove(pglist_t *pl, uint32_t blk);

size_t page_alloc_color(uint32_t rank, page_type_t type, uint32_t period, uint32_t phase, const char *file, int line);
size_t page_alloc(uint32_t rank, page_type_t type, const char *file, int line);
void page_free(size_t pa);

void pagelist_alloc(pglist_t *pl, uint32_t num, page_type_t type, const char *file, int line);
void pagelist_free(pglist_t *pl);

uint32_t page_free_count();

#define PAGE_ALLOC(rank, type) page_alloc(rank, type, __FILE__, __LINE__)

INIT_TEXT void page_init(size_t pa_start, size_t pa_end);
INIT_TEXT void pages_add(size_t start, size_t end);

#endif // PAGE_H
