#include "page.h"
#include <arch_impl.h>
#include "early_alloc.h"
#include <library/debug.h>



// 每个物理页都有这个结构体，记录相关信息
typedef union page_info {
    struct {
        uint16_t ent_num;    // 有效页表条目数量
    };
} page_info_t;

typedef struct page {
    pfn_t prev;
    pfn_t next;

    uint32_t head : 1;   // 是不是块中第一个页
    uint32_t rank : 4;   // 所在块的大小，head==1 才有效
    uint32_t type : 4;   // 所在块的类型，head==1 才有效

    page_info_t info;
} page_t;


// 相同类型的块可以组成链表（双向不循环）
typedef struct pglist {
    pfn_t head;
    pfn_t tail;
} pglist_t;


// 页描述符数组，每个物理页都对应一个结构体
static CONST pfn_t g_page_num = 0;
static page_t *g_pages = NULL;


// 未分配的物理页块按不同大小组成链表（伙伴算法）
#define RANK_NUM 16
static pglist_t g_blocks[RANK_NUM];


//------------------------------------------------------------------------------
// 页块
//------------------------------------------------------------------------------

// 返回页所在的块
pfn_t page_block_head(pfn_t pfn) {
    ASSERT(pfn < g_page_num);
    while (!g_pages[pfn].head && pfn) {
        pfn &= pfn - 1;
    }
    return pfn;
}

// 返回块大小
pfn_t page_block_size(pfn_t blk) {
    ASSERT(blk < g_page_num);
    ASSERT(g_pages[blk].head);
    return 1 << g_pages[blk].rank;
}

page_info_t *page_block_info(size_t pa) {
    ASSERT(0 == (pa & (PAGE_SIZE - 1)));

    pa >>= PAGE_SHIFT;
    ASSERT(pa < g_page_num);
    ASSERT(g_pages[pa].head);

    return &g_pages[pa].info;
}

// 将一个块分割为两个大小相等的块，返回后一个块的编号
static pfn_t block_split(pfn_t blk) {
    ASSERT(blk < g_page_num);
    ASSERT(g_pages[blk].head);
    ASSERT(0 != g_pages[blk].rank);

    size_t rank = g_pages[blk].rank - 1;
    pfn_t sib = blk + (1 << rank);

    g_pages[blk].rank = rank;
    g_pages[sib].head = 1;
    g_pages[sib].rank = rank;
    g_pages[sib].type = g_pages[blk].type;

    return sib;
}

// 将两个伙伴块合并为一个更大的块，返回合并后的块地址
static pfn_t block_merge(pfn_t a, pfn_t b) {
    ASSERT(a < g_page_num);
    ASSERT(b < g_page_num);
    ASSERT(g_pages[a].head);
    ASSERT(g_pages[b].head);
    ASSERT(g_pages[a].rank < RANK_NUM - 1);
    ASSERT(g_pages[a].rank == g_pages[b].rank);
    ASSERT((a ^ b) == page_block_size(a));

    g_pages[a & b].rank++;
    g_pages[a | b].head = 0;

    return a & b;
}


//------------------------------------------------------------------------------
// 页块链表
//------------------------------------------------------------------------------

int pglist_contains(pglist_t *pl, pfn_t blk) {
    ASSERT(NULL != pl);

    for (pfn_t i = pl->head; INVALID_PFN != i; i = g_pages[i].next) {
        ASSERT(i < g_page_num);
        ASSERT(g_pages[i].head);

        if (blk == i) {
            return 1;
        }
    }

    return 0;
}

void pglist_push_head(pglist_t *pl, pfn_t blk) {
    ASSERT(NULL != pl);
    ASSERT(INVALID_PFN != blk);
    ASSERT(!pglist_contains(pl, blk));

    pfn_t next = pl->head;
    pl->head = blk;
    g_pages[blk].prev = INVALID_PFN;
    g_pages[blk].next = next;

    if (INVALID_PFN == next) {
        pl->tail = blk;
    } else {
        g_pages[next].prev = blk;
    }
}

void pglist_push_tail(pglist_t *pl, pfn_t blk) {
    ASSERT(NULL != pl);
    ASSERT(INVALID_PFN != blk);
    ASSERT(!pglist_contains(pl, blk));

    pfn_t prev = pl->tail;
    pl->tail = blk;
    g_pages[blk].prev = prev;
    g_pages[blk].next = INVALID_PFN;

    if (INVALID_PFN == prev) {
        pl->head = blk;
    } else {
        g_pages[prev].next = blk;
    }
}

pfn_t pglist_pop_head(pglist_t *pl) {
    ASSERT(NULL != pl);

    pfn_t head = pl->head;
    if (INVALID_PFN == head) {
        return INVALID_PFN;
    }

    pfn_t next = g_pages[head].next;
    pl->head = next;
    if (INVALID_PFN == next) {
        pl->tail = INVALID_PFN;
    } else {
        g_pages[next].prev = INVALID_PFN;
    }

    return head;
}

pfn_t pglist_pop_tail(pglist_t *pl) {
    ASSERT(NULL != pl);

    pfn_t tail = pl->tail;
    if (INVALID_PFN == tail) {
        return INVALID_PFN;
    }

    pfn_t prev = g_pages[tail].prev;
    pl->tail = prev;
    if (INVALID_PFN == prev) {
        pl->head = INVALID_PFN;
    } else {
        g_pages[prev].next = INVALID_PFN;
    }

    return tail;
}

void pglist_remove(pglist_t *pl, pfn_t blk) {
    ASSERT(NULL != pl);
    ASSERT(INVALID_PFN != blk);
    ASSERT(pglist_contains(pl, blk));

    pfn_t prev = g_pages[blk].prev;
    pfn_t next = g_pages[blk].next;

    if (INVALID_PFN == prev) {
        pl->head = next;
    } else {
        g_pages[prev].next = next;
    }
    if (INVALID_PFN == next) {
        pl->tail = prev;
    } else {
        g_pages[next].prev = prev;
    }
}




//------------------------------------------------------------------------------
// 物理内存管理初始化
//------------------------------------------------------------------------------

INIT_TEXT void page_desc_init(size_t end) {
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

INIT_TEXT void page_set_type(size_t start, size_t end, uint8_t type) {
    ASSERT(0 != g_page_num);
    ASSERT(NULL != g_pages);

    start >>= PAGE_SHIFT;
    end += PAGE_SIZE - 1;
    end >>= PAGE_SHIFT;

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
