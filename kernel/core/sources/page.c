// 物理页面管理/分配

#include <wheel.h>


// 物理页类型
typedef enum page_type {
    PT_INVALID  = 0,    // 不存在
    PT_FREE,            // 未分配的可用内存
    PT_KERNEL,          // 被内核代码数据占用
} page_type_t;

// 页描述符
typedef struct page {
    pfn_t prev;
    pfn_t next;

    size_t type;
} page_t;

// 相同类型的块可以组成链表（双向不循环）
typedef struct pglist {
    pfn_t head;
    pfn_t tail;
} pglist_t;



// 全局页描述符数组，每个物理页面都对应一个结构体
static CONST pfn_t g_page_num = 0;
static CONST page_t *g_pages;



static int pglist_contains(pglist_t *pl, pfn_t blk) {
    for (pfn_t i = pl->head; INVALID_PFN != i; i = g_pages[i].next) {
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






INIT_TEXT void page_init(size_t num) {
    g_page_num = (pfn_t)num;
    g_pages = (page_t *)early_alloc_rw(g_page_num * sizeof(page_t *));

    for (pfn_t i = 0; i < g_page_num; ++i) {
        g_pages[i].prev = INVALID_PFN;
        g_pages[i].next = INVALID_PFN;
        g_pages[i].type = PT_INVALID;
    }
}

// 将一段内存标记为可用范围
// TODO 同时指定使用情况，即当前为 FREE 还是 BUSY
INIT_TEXT void page_add(pfn_t start, pfn_t end) {
    ASSERT(start < end);
    ASSERT(end <= g_page_num);

    for (pfn_t i = start; i < end; ++i) {
        g_pages[i].type = PT_FREE;
    }
}
