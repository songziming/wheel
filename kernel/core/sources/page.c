// 物理页面管理/分配

#include <page.h>
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

    size_t head : 1;    // 是不是块中第一个页
    size_t rank : 4;   // 所在块的大小，head==1 才有效
    size_t type : 4;   // 所在块的类型，head==1 才有效

    page_info_t info;
} page_t;

// 相同类型的块可以组成链表（双向不循环）
typedef struct pglist {
    pfn_t head;
    pfn_t tail;
} pglist_t;



// 全局页描述符数组，每个物理页面都对应一个结构体
static CONST pfn_t g_page_num = 0;
static CONST page_t *g_pages = NULL;

// 未分配的页块按不同大小组成链表
#define RANK_NUM 16
static pglist_t g_free_blocks[RANK_NUM];




//------------------------------------------------------------------------------
// 连续的物理页组成块
//------------------------------------------------------------------------------

// 返回页所在的块
static pfn_t block_head(pfn_t pfn) {
    ASSERT(pfn < g_page_num);
    while (0 == g_pages[pfn].head) {
        pfn &= pfn - 1;
    }
    return pfn;
}

// 返回块大小
static pfn_t block_size(pfn_t blk) {
    ASSERT(blk < g_page_num);
    ASSERT(g_pages[blk].head);
    return 1 << g_pages[blk].rank;
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
    ASSERT((a ^ b) == block_size(a));

    g_pages[a & b].rank++;
    g_pages[a | b].head = 0;

    return a & b;
}



//------------------------------------------------------------------------------
// 相同类型的页块可以组成链表
//------------------------------------------------------------------------------

int pglist_contains(pglist_t *pl, pfn_t blk) {
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


//------------------------------------------------------------------------------
// 物理页面分配/回收
//------------------------------------------------------------------------------


// 回收一个页块
static void block_free(pfn_t blk) {
    ASSERT(blk < g_page_num);
    ASSERT(g_pages[blk].head);

    size_t rank = g_pages[blk].rank;
    pfn_t size = 1 << rank;
    ASSERT(blk + size <= g_page_num);
    ASSERT(0 == (blk & (size - 1)));

    // 不断检查伙伴块，尝试合并为更大的块
    for (; rank < RANK_NUM - 1; ++rank, size <<= 1) {
        pfn_t sib = blk ^ size;

        // 合并的条件：伙伴块没有越界、rank 相同、类型是 FREE
        // 一旦发现无法合并就退出循环
        if ((sib >= g_page_num)
        ||  (0 == g_pages[sib].head)
        ||  (rank != g_pages[sib].rank)
        ||  (PT_FREE != g_pages[sib].type)) {
            break;
        }

        // 将伙伴从 free-list 中移除，合并为更大的块
        pglist_remove(&g_free_blocks[rank], sib);
        blk = block_merge(blk, sib);
    }

    // 已经合并到最大，标记为 FREE
    // 将合并之后的 block 添加到链表（的开头）
    g_pages[blk].type = PT_FREE;
    pglist_push_head(&g_free_blocks[rank], blk);
}

// 申请一个页块，起始页号必须是 N*period+phase
// 限制起始页号可以实现页面着色，优化缓存性能
static pfn_t block_alloc(uint8_t rank, pfn_t period, pfn_t phase) {
    ASSERT(rank < RANK_NUM);
    ASSERT(0 == (period & (period - 1)));
    ASSERT(phase < period);

    pfn_t size = 1 << rank;
    ASSERT(0 == (phase & (size - 1)));

    for (uint8_t blk_rank = rank; blk_rank < RANK_NUM; ++blk_rank, size <<= 1) {
        // 计算 phase 相对于当前 rank 的偏移量
        pfn_t blk_phase = phase & ~(size - 1);

        // 遍历本层的 free block，寻找目标 color
        pfn_t blk = g_free_blocks[blk_rank].head;
        for (; INVALID_PFN != blk; blk = g_pages[blk].next) {
            if ((blk & (period - 1)) == blk_phase) {
                break;
            }
        }

        if (INVALID_PFN == blk) {
            continue;
        }

        // 将这个 block 标记为已分配
        pglist_remove(&g_free_blocks[blk_rank], blk);
        g_pages[blk].type = PT_KERNEL;

        // 如果这个块超过所需，则将 block 分割为两个子块，返回不需要的部分
        // 根据 phase 决定每一级回收前一半还是后一半
        for (; blk_rank > rank; --blk_rank) {
            pfn_t sib = block_split(blk);

            if ((blk ^ sib) & phase) {
                block_free(blk); // 回收前一半，保留后一半
                blk = sib;
            } else {
                block_free(sib); // 回收后一半，保留前一半
            }
        }

        return blk;
    }

    return INVALID_PFN;
}




//------------------------------------------------------------------------------
// 公开的函数
//------------------------------------------------------------------------------

// 传入物理内存的上限
INIT_TEXT void page_init(size_t end) {
    ASSERT(0 == g_page_num);
    ASSERT(NULL == g_pages);

    end >>= PAGE_SHIFT;

    if (end > INVALID_PFN) {
        end = INVALID_PFN;
    }
    g_page_num = (pfn_t)end;
    g_pages = (page_t *)early_alloc_rw(g_page_num * sizeof(page_t *));

    for (pfn_t i = 0; i < g_page_num; ++i) {
        g_pages[i].prev = INVALID_PFN;
        g_pages[i].next = INVALID_PFN;
        g_pages[i].type = PT_INVALID;
        g_pages[i].head = 0;
    }

    for (int i = 0; i < RANK_NUM; ++i) {
        g_free_blocks[i].head = INVALID_PFN;
        g_free_blocks[i].tail = INVALID_PFN;
    }
}

// 将一段内存标记为可用范围
// TODO 同时指定使用情况，即当前为 FREE 还是 BUSY
INIT_TEXT void page_add(size_t start, size_t end) {
    ASSERT(0 != g_page_num);
    ASSERT(NULL != g_pages);

    start = (start + PAGE_SIZE - 1) >> PAGE_SHIFT;
    end = end >> PAGE_SHIFT;

    if (end > g_page_num) {
        end = g_page_num;
    }
    if (start >= end) {
        return;
    }
    // for (size_t i = start; i < end; ++i) {
    //     g_pages[i].type = PT_FREE;
    // }

    // 这一段内存不一定是按块对齐的
    // 尽可能使用更大的块
    while (start < end) {
        int rank = __builtin_ctz(start);
        if ((rank >= RANK_NUM) || (start == 0)) {
            rank = RANK_NUM - 1;
        }
        while ((start + (1UL << rank)) > end) {
            --rank;
        }

        // 创建一个块，并将其回收
        g_pages[start].head = 1;
        g_pages[start].rank = rank;
        block_free(start);
        start += (1UL << rank);
    }
}

// 申请一个物理页
size_t page_alloc() {
    pfn_t pg = block_alloc(0, 1, 0);
    if (INVALID_PFN) {
        return INVALID_ADDR;
    }
    return (size_t)pg << PAGE_SHIFT;
}

// 回收一个物理页
void page_free(size_t pa) {
    ASSERT(0 == (pa & (PAGE_SIZE - 1)));

    pa >>= PAGE_SHIFT;
    ASSERT(PT_FREE != g_pages[pa].type);
    ASSERT(0 == g_pages[pa].rank);

    block_free((pfn_t)pa);
}

page_info_t *page_info(size_t pa) {
    ASSERT(0 == (pa & (PAGE_SIZE - 1)));

    return &g_pages[pa >> PAGE_SHIFT].info;
}
