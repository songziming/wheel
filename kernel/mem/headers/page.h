#ifndef PAGE_H
#define PAGE_H

#include <def.h>


// 物理页类型
typedef enum page_type {
    PT_INVALID  = 0,    // 不存在或不可用
    PT_FREE,            // 未分配的可用内存
    PT_KERNEL,          // 被内核代码数据占用
    PT_PGTBL,           // 页表结构使用
    PT_KERNEL_STACK,    // 任务的内核栈
    PT_CACHE,           // 块设备缓存
} page_type_t;

// 每个物理页都有这个结构体，记录相关信息
typedef union page_info {
    struct {
        uint16_t ent_num;    // 有效页表条目数量
    };
} page_info_t;

// 相同类型的块可以组成链表（双向不循环）
typedef struct pglist {
    pfn_t head;
    pfn_t tail;
} pglist_t;



// pfn_t prev_page_block(pfn_t blk);
// pfn_t next_page_block(pfn_t blk);

pfn_t page_block_head(pfn_t pfn);
pfn_t page_block_size(pfn_t blk);
page_info_t *page_block_info(size_t pa);


int pglist_contains(pglist_t *pl, pfn_t blk);
void pglist_push_head(pglist_t *pl, pfn_t blk);
void pglist_push_tail(pglist_t *pl, pfn_t blk);
pfn_t pglist_pop_head(pglist_t *pl);
pfn_t pglist_pop_tail(pglist_t *pl);
void pglist_remove(pglist_t *pl, pfn_t blk);


INIT_TEXT void page_init(size_t end);
INIT_TEXT void page_add(size_t start, size_t end, page_type_t type);

size_t pages_alloc(int rank, page_type_t type); // 申请一个物理页，返回物理地址
void pages_free(size_t pa);

#endif // PAGE_H
