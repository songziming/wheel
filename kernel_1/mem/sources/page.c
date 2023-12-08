// 物理内存管理，伙伴算法

#include <page.h>
#include <arch_interface.h>

#include <debug.h>
#include <spin.h>
#include <libk.h>
#include <dllist.h>





// 将 2^n 个对齐的连续 page 组成 block，其中 n 是 rank
// rank 可以用四比特表示，16 个取值
#define RANK_NUM 16

// 页描述符数组
CONST size_t g_page_count = 0;
static CONST page_t *g_pages = NULL;

// 记录所有 FREE 类型的 block，按 rank 分级
// 相同 rank 的 block 组成链表
static page_list_t g_free_blocks[RANK_NUM];

// 申请/释放页块、操作页属性都需要获取自旋锁
static spin_t g_pages_lock = SPIN_INIT;


#if 0
// 返回物理页所在的 block 的起始地址
static pfn_t container_block(pfn_t pfn) {
    while (INVALID_RANK == g_pages[pfn].rank) {
        pfn &= pfn - 1;
    }
    return pfn;
}
#endif


//------------------------------------------------------------------------------
// 页帧链表
//------------------------------------------------------------------------------

void page_list_push_head(page_list_t *pl, pfn_t blk) {
    ASSERT(NULL != pl);
    ASSERT(INVALID_PFN != blk);

#ifdef DEBUG
    // blk 不能位于链表中
    int block_in_list = 0;
    for (pfn_t i = pl->head; INVALID_PFN != i; i = g_pages[i].next) {
        if (i == blk) {
            block_in_list = 1;
            break;
        }
    }
    ASSERT(!block_in_list);
#endif

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

#ifdef DEBUG
    // blk 不能位于链表中
    int block_in_list = 0;
    for (pfn_t i = pl->head; INVALID_PFN != i; i = g_pages[i].next) {
        if (i == blk) {
            block_in_list = 1;
            break;
        }
    }
    ASSERT(!block_in_list);
#endif

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

#ifdef DEBUG
    // blk 必须位于链表中
    int block_in_list = 0;
    for (pfn_t i = pl->head; INVALID_PFN != i; i = g_pages[i].next) {
        if (i == blk) {
            block_in_list = 1;
            break;
        }
    }
    ASSERT(block_in_list);
#endif

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
// 对外公开页帧数据结构体
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
    ASSERT(pfn < g_page_count);
    ASSERT(0 != g_pages[pfn].isblock);

    return (void *)&g_pages[pfn].u;
}



//------------------------------------------------------------------------------
// 私有函数，不涉及自旋锁
//------------------------------------------------------------------------------

// 释放页块，与相邻块合并
static void _block_free(pfn_t blk) {
    ASSERT(blk < g_page_count);
    ASSERT(0 != g_pages[blk].isblock);

    size_t rank = g_pages[blk].rank;
    pfn_t size = 1 << rank;
    ASSERT(blk + size <= g_page_count);
    ASSERT(0 == (blk & (size - 1)));

    // 不断检查伙伴块，尝试合并为更大的块
    for (; rank < RANK_NUM - 1; ++rank, size <<= 1) {
        pfn_t sib = blk ^ size;

        // 合并的条件：伙伴块没有越界、rank 相同、类型是 FREE
        // 一旦发现无法合并就退出循环
        if ((sib >= g_page_count)
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

// 分配一个 2^rank 大小的块，起始页号对 period 取余结果是 phase
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

// 分配最小的块，没有分割
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

#if 0
// 回收若干连续页
static void _pages_free(pfn_t pfn, pfn_t num) {
    pfn_t end = pfn + num;
    if (end > g_page_count) {
        end = g_page_count;
    }

#ifdef DEBUG
    // 检查这段范围是不是完整的 block，block 类型不能为 PT_FREE
    pfn_t p = pfn;
    while (p < end) {
        ASSERT(INVALID_RANK != g_pages[p].rank);
        ASSERT(PT_FREE != g_pages[p].type);

        pfn_t blksize = 1 << g_pages[p].rank;
        for (pfn_t i = 1; i < blksize; ++i) {
            ASSERT(INVALID_RANK == g_pages[p + i].rank);
        }

        p += blksize;
    }
    ASSERT(end == p);
#endif

    // 将每个描述符的 rank 设为 -1
    // 这样添加 block 时，只需要修改首页的 rank 标记
    for (pfn_t i = pfn; i < end; ++i) {
        g_pages[i].rank = INVALID_RANK;
    }

    // 从前到后，以尽可能大的 block 逐个回收
    while (pfn < end) {
        int rank = __builtin_ctz(pfn);
        if ((0 == pfn) || (rank >= RANK_NUM)) {
            rank = RANK_NUM - 1;
        }
        while (pfn + (1U << rank) > end) {
            --rank;
        }

        g_pages[pfn].rank = rank;
        _block_free(pfn, rank);
        pfn += 1U << rank;
    }
}

// 申请若干连续物理页
static pfn_t _pages_alloc(pfn_t num, page_type_t type) {
    ASSERT(num > 0);
    ASSERT(PT_FREE != type);

    for (uint8_t rank = 0; rank < RANK_NUM; ++rank) {
        pfn_t size = 1 << rank;
        if (size < num) {
            continue;
        }

        pfn_t blk = _block_alloc(rank, type, 1, 0);
        if (INVALID_PFN == blk) {
            return INVALID_PFN;
        }

        // 回收开头的多余的物理页
        if (size > num) {
            for (pfn_t i = 0; i < size; ++i) {
                g_pages[blk + i].rank = 0;
                g_pages[blk + i].type = type;
            }
            _pages_free(blk, size - num);
            blk += size - num;
        }

        return blk;
    }

    // 超过了最大 rank 的表示范围
    return INVALID_PFN;
}
#endif


//------------------------------------------------------------------------------
// 公开函数
//------------------------------------------------------------------------------

// 初始化函数，传入页描述符数组，将所有页标记为不可用
// 同时指定 L1 一组缓存的页数
INIT_TEXT void pages_init(size_t npages) {
    ASSERT(npages <= INVALID_PFN);

    // 分配页描述符
    if (npages > INVALID_PFN) {
        dbg_print("physical memory exceeds %u!\n", INVALID_PFN);
        npages = INVALID_PFN;
    }
    g_page_count = npages;
    g_pages = early_alloc(npages * sizeof(page_t));

    // 将每个页标记为无效
    for (size_t i = 0; i < g_page_count; ++i) {
        g_pages[i].rank = 0;
        g_pages[i].type = PT_INVALID;
    }

    // 伙伴块链表初始为空
    for (int i = 0; i < RANK_NUM; ++i) {
        g_free_blocks[i] = PAGE_LIST_EMPTY;
    }
}

// 用于启动阶段添加可用物理内存范围，不涉及自旋锁
INIT_TEXT void add_free_pages(pfn_t pfn, pfn_t num) {
    pfn_t end = pfn + num;
    if (end > g_page_count) {
        end = g_page_count;
    }

    // 将每个页标记为可用，rank标记为-1
    // 这样添加 block 时，只需要修改每个 block 开头页的 rank 标记
    for (pfn_t i = pfn; i < end; ++i) {
        g_pages[i].isblock = 0;
        // g_pages[i].rank = INVALID_RANK;
    }

    // 从前到后，尝试以最大的 rank 组成块
    while (pfn < end) {
        int rank = __builtin_ctz(pfn);
        if ((0 == pfn) || (rank >= RANK_NUM)) {
            rank = RANK_NUM - 1;
        }
        while (pfn + (1U << rank) > end) {
            --rank;
        }

        g_pages[pfn].isblock = 1;
        g_pages[pfn].rank = rank;
        _block_free(pfn);
        pfn += 1U << rank;
    }
}

// 分配一个 2^rank 大小的块，颜色任意
// TODO 检查color直方图，选择可用页面数量最多的color
pfn_t page_block_alloc(uint8_t rank, page_type_t type) {
    spin_take(&g_pages_lock);
    pfn_t pn = _block_alloc(rank, type, 1, 0);
    spin_give(&g_pages_lock);
    return pn;
}

pfn_t page_block_alloc_phase(uint8_t rank, page_type_t type, pfn_t period, pfn_t phase) {
    spin_take(&g_pages_lock);
    pfn_t pn = _block_alloc(rank, type, period, phase);
    spin_give(&g_pages_lock);
    return pn;
}

// 回收一个 block
void page_block_free(pfn_t blk) {
    spin_take(&g_pages_lock);
    ASSERT(0 != g_pages[blk].isblock);
    ASSERT(PT_FREE != g_pages[blk].type);
    _block_free(blk);
    spin_give(&g_pages_lock);
}


//------------------------------------------------------------------------------
// 快速页面分配、释放（LRU 缓存）
//------------------------------------------------------------------------------

static PCPU_DATA page_list_t g_page_cache = PAGE_LIST_EMPTY;
static PCPU_DATA int g_page_cache_num = 0;

// 从页缓存中快速申请一个页
pfn_t page_cache_alloc(page_type_t type) {
    ASSERT(PT_FREE != type);

    page_list_t *pl = this_ptr(&g_page_cache);
    int *num = this_ptr(&g_page_cache_num);

    // 从缓存里获取一个页，如果获取成功则直接返回
    pfn_t pn = page_list_pop_head(pl);
    if (INVALID_PFN != pn) {
        ASSERT(PT_CACHED == g_pages[pn].type);
        g_pages[pn].type = type;
        --(*num);
        return pn;
    }

    // 缓存不足，申请一个块
    spin_take(&g_pages_lock);
    pfn_t blk = _block_alloc_smallest(PT_CACHED);
    if (INVALID_PFN == blk) {
        spin_give(&g_pages_lock);
        return INVALID_PFN;
    }

    // 申请的块超过了缓存的上限，将多余部分放回伙伴系统
    uint8_t rank = g_pages[blk].rank;
    pfn_t size = 1 << rank;
    while (size > PAGE_CACHE_MAX) {
        --rank;
        size >>= 1;
        g_pages[blk].rank = rank;
        g_pages[blk + size].isblock = 1;
        g_pages[blk + size].rank = rank;
        // g_pages[blk + size].type = type;
        _block_free(blk + size);
    }
    spin_give(&g_pages_lock);

    // 将这个块拆分成若干独立页面，（除了第一页）放入缓存
    for (pfn_t i = 1; i < size; ++i) {
        g_pages[blk + i].isblock = 1;
        g_pages[blk + i].rank = 0;
        g_pages[blk + i].type = PT_CACHED;
        page_list_push_head(pl, blk + i);
    }
    *num = size - 1;

    // 开头第一个页返回
    g_pages[blk].isblock = 1;
    g_pages[blk].rank = 0;
    g_pages[blk].type = type;
    return blk;
}

// 快速释放一个页，放回缓存
void page_cache_free(pfn_t pn) {
    page_list_t *pl = this_ptr(&g_page_cache);
    int *num = this_ptr(&g_page_cache_num);

    page_list_push_head(pl, pn);
    ++(*num);

    // 如果缓存页太多，就把多余的页放回 page-alloc
    // TODO 优先回收能与周围块合并的页
    while ((*num) > PAGE_CACHE_MAX) {
        pn = page_list_pop_tail(pl);
        page_block_free(pn);
        --(*num);
    }
}

//------------------------------------------------------------------------------
// 为方便使用提供的函数
//------------------------------------------------------------------------------

// 分配一个物理页面，如果失败就停机
// TODO 根据color直方图选择最合适的颜色
pfn_t page_alloc_or_die(page_type_t type) {
    pfn_t pn = page_cache_alloc(type);
    if (INVALID_PFN == pn) {
        dbg_print("fatal: cannot alloc page frame!\n");
        while (1) {
            cpu_halt();
        }
    }
    return pn;
}


