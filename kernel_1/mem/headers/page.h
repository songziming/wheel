#ifndef MEM_PAGE_H
#define MEM_PAGE_H

#include <base.h>

// 块类型，记录在块首页的 type 字段
typedef enum page_type {
    PT_INVALID  = 0,    // 不可用、被内核镜像占用
    PT_FREE,            // 未分配
    PT_CACHED,
    PT_STACK,           // 任务栈
    PT_PAGETBL,         // 页表
    PT_POOL,            // 可自动扩容的内存池
    PT_HEAP,            // 可自动扩容的堆
} page_type_t;

typedef struct pool_info {
    dlnode_t    free;   // 属于此block的空闲对象的空闲对象链表
} pool_info_t;

typedef struct page_table_info {
    int ref_count;  // 这一级页表被多少个地址空间使用
    int ent_count;  // 元素数量
} page_table_info_t;

// 页描述符，每个物理页都对应一个结构体
// 大小应该按 cache line 对齐，避免 false sharing
// 描述符结构和数组私有，其他模块不能访问
typedef struct page {
    pfn_t       prev;
    pfn_t       next;

    size_t      isblock : 1;    // 是不是块中第一个页
    size_t      rank : 4;   // 所在块的大小，isblock==1 才有效
    size_t      type : 4;   // 所在块的类型，isblock==1 才有效
    size_t      virt : VIRT_ADDR_BITS - PAGE_SHIFT; // 映射到的虚拟地址，页对齐，不用保存符号扩展部分

    union {
        pool_info_t pool;
        page_table_info_t table;
    } u;
} page_t;

// 相同类型的块可以组成（双向不循环）链表
typedef struct page_list {
    pfn_t head;
    pfn_t tail;
} page_list_t;

#define PAGE_LIST_EMPTY ((page_list_t){ INVALID_PFN, INVALID_PFN })

// 页块链表
void page_list_init(page_list_t *pl);
void page_list_push_head(page_list_t *pl, pfn_t blk);
void page_list_push_tail(page_list_t *pl, pfn_t blk);
pfn_t page_list_pop_head(page_list_t *pl);
pfn_t page_list_pop_tail(page_list_t *pl);
void page_list_remove(page_list_t *pl, pfn_t blk);


// 物理页分配/回收
extern CONST size_t g_page_count;

INIT_TEXT void pages_init(size_t npages);
INIT_TEXT void add_free_pages(pfn_t pfn, pfn_t num);

pfn_t page_block(pfn_t pfn);
void *page_data(pfn_t pfn);

pfn_t page_block_alloc(uint8_t rank, page_type_t type);
pfn_t page_block_alloc_phase(uint8_t rank, page_type_t type, pfn_t period, pfn_t phase);
void  page_block_free(pfn_t blk);
// pfn_t pages_alloc(pfn_t n, page_type_t type);
// void pages_free(pfn_t pfn, pfn_t n);


pfn_t page_cache_alloc(page_type_t type);
void page_cache_free(pfn_t pn);


pfn_t page_alloc_or_die(page_type_t type);

#endif // MEM_PAGE_H
