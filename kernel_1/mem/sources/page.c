#include <page.h>
#include <debug.h>


// 能否完全由 page.c 管理页描述符，结构体不公开
// 通过接口函数允许其他模块访问指定的字段


// 页描述符数组
static CONST pfn_t g_page_start = 0;
static CONST pfn_t g_page_num = 0;
static CONST page_t *g_pages = NULL;

// 相同大小的空闲块组成链表，块最大 2^15
#define RANK_NUM 16
static page_list_t g_free_blocks[RANK_NUM];


//------------------------------------------------------------------------------
// 页帧链表
//------------------------------------------------------------------------------

#ifdef DEBUG

static int contains(page_list_t *pl, pfn_t blk) {
    for (pfn_t i = pl->head; INVALID_PFN != i; i = g_pages[i].next) {
        if (blk == i) {
            return 1;
        }
    }
    return 0;
}

#endif // DEBUG

void page_list_push_head(page_list_t *pl, pfn_t blk) {
    ASSERT(NULL != pl);
    ASSERT(INVALID_PFN != blk);
    ASSERT(!contains(pl, blk));

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

void page_list_push_tail(page_list_t *pl, pfn_t blk) {
    ASSERT(NULL != pl);
    ASSERT(INVALID_PFN != blk);
    ASSERT(!contains(pl, blk));

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

pfn_t page_list_pop_head(page_list_t *pl) {
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

pfn_t page_list_pop_tail(page_list_t *pl) {
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

void page_list_remove(page_list_t *pl, pfn_t blk) {
    ASSERT(NULL != pl);
    ASSERT(INVALID_PFN != blk);
    ASSERT(contains(pl, blk));

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
// 公开函数
//------------------------------------------------------------------------------

// 返回所在块的地址
pfn_t page_block(pfn_t pfn) {
    while (0 == g_pages[pfn].isblock) {
        pfn &= pfn - 1;
    }
    return pfn;
}

// 不同类型的page
void *page_data(pfn_t pfn) {
    ASSERT(pfn < g_page_num);
    ASSERT(0 != g_pages[pfn].isblock);

    return (void *)&g_pages[pfn].u;
}


//------------------------------------------------------------------------------
// 伙伴算法管理物理页
//------------------------------------------------------------------------------

// 释放页块，与相邻块合并
static void _block_free(pfn_t blk) {
    ASSERT(blk < g_page_num);
    ASSERT(0 != g_pages[blk].isblock);

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
        ||  (0 == g_pages[sib].isblock)
        ||  (rank != g_pages[sib].rank)
        ||  (PT_FREE != g_pages[sib].type)) {
            break;
        }

        // 将伙伴从 free-list 中移除
        page_list_remove(&g_free_blocks[rank], sib);

        // 合并为更大的块
        g_pages[blk | size].isblock = 0;
        // g_pages[blk | size].rank = INVALID_RANK;
        blk &= ~size;
    }

    // 已经合并到最大，标记为 FREE
    g_pages[blk].isblock = 1;
    g_pages[blk].rank = rank;
    g_pages[blk].type = PT_FREE;

    // 将合并之后的 block 添加到链表（的开头）
    page_list_push_head(&g_free_blocks[rank], blk);
}

// 分配一个 2^rank 大小的块，要求起始页号是 N*period+phase
static pfn_t _block_alloc(uint8_t rank, page_type_t type, pfn_t period, pfn_t phase) {
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
        page_list_remove(&g_free_blocks[blk_rank], blk);
        g_pages[blk].type = type;

        // 如果这个块超过所需，则将 block 分割为两个子块，返回不需要的部分
        // 根据 phase 决定每一级回收前一半还是后一半
        for (; blk_rank > rank; --blk_rank) {
            pfn_t half = 1U << (blk_rank - 1);
            g_pages[blk].rank = blk_rank - 1;
            g_pages[blk + half].isblock = 1;
            g_pages[blk + half].rank = blk_rank - 1;
            // g_pages[blk + half].type = type;
            if (phase & half) {
                // 回收前一半，保留后一半
                _block_free(blk);
                blk += half;
            } else {
                // 回收后一半，保留前一半
                _block_free(blk + half);
            }
        }

        return blk;
    }

    return INVALID_PFN;
}

// 分配最小的可用块，没有分割
static pfn_t _block_alloc_smallest(page_type_t type) {
    for (uint8_t rank = 0; rank < RANK_NUM; ++rank) {
        pfn_t blk = page_list_pop_head(&g_free_blocks[rank]);
        if (INVALID_PFN == blk) {
            continue;
        }

        g_pages[blk].type = type;
        return blk;
    }

    return INVALID_PFN;
}




//------------------------------------------------------------------------------
// 初始化函数
//------------------------------------------------------------------------------


// 启动阶段分配页描述符数组
INIT_TEXT void pages_init(size_t start, size_t end) {
    ASSERT(0 == g_page_num);
    ASSERT(NULL == g_pages);
    ASSERT(start < end);

    start  += PAGE_SIZE - 1;
    start >>= PAGE_SHIFT;
    end   >>= PAGE_SHIFT;

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

    // 这样，页描述符数组就可以用常规页号访问
    g_pages -= g_page_start;
}

// 启动阶段添加可用内存
INIT_TEXT void pages_add(pfn_t start, pfn_t end) {
    ASSERT(0 != g_page_num);
    ASSERT(NULL != g_pages);
    ASSERT(start >= g_page_start);
    ASSERT(end >= g_page_start);

    if (start == end) {
        return;
    }

    for (pfn_t i = start; i <= end; ++i) {
        g_pages[i].prev = i;
        g_pages[i].next = i;
    }

    klog(">>> adding page range %x~%x\n", start, end);
}
