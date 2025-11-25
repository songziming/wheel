#include "page.h"
#include "early_alloc.h"
#include <arch_api.h>
#include <kstring.h>
#include <debug.h>


CONST uint32_t g_page_start;
CONST uint32_t g_page_end;
CONST page_t *g_pages;

// 伙伴块一共有 16 个大小级别
#define RANK_NUM 16
static pglist_t g_blocks[RANK_NUM];


//------------------------------------------------------------------------------
// 块操作函数
//------------------------------------------------------------------------------

// // 返回块大小
// uint32_t blk_size(uint32_t pfn) {
//     ASSERT(pfn >= g_page_start);
//     ASSERT(pfn < g_page_end);
//     ASSERT(g_pages[pfn].head);
//     return 1U << g_pages[pfn].rank;
// }



//------------------------------------------------------------------------------
// 页块链表
//------------------------------------------------------------------------------

void pglist_push_tail(pglist_t *pl, uint32_t blk) {
    uint32_t tail = pl->tail;
    pl->tail = blk;

    g_pages[blk].prev = tail;
    g_pages[blk].next = 0;

    if (0 == tail) {
        pl->head = blk;
    } else {
        g_pages[tail].next = blk;
    }
}

void pglist_push_head(pglist_t *pl, uint32_t blk) {
    uint32_t head = pl->head;
    pl->head = blk;

    g_pages[blk].prev = 0;
    g_pages[blk].next = head;

    if (0 == head) {
        pl->tail = blk;
    } else {
        g_pages[head].prev = blk;
    }
}

void pglist_remove(pglist_t *pl, uint32_t blk) {
    uint32_t prev = g_pages[blk].prev;
    uint32_t next = g_pages[blk].next;

    if (prev) {
        g_pages[prev].next = next;
    } else {
        pl->head = next;
    }
    if (next) {
        g_pages[next].prev = prev;
    } else {
        pl->tail = prev;
    }
}


//------------------------------------------------------------------------------
// 物理页块级别的分配释放
//------------------------------------------------------------------------------

// 释放一个页块
static void block_free_nolock(uint32_t blk) {
    ASSERT(blk >= g_page_start);
    ASSERT(blk < g_page_end);
    ASSERT(g_pages[blk].head);

    // 不断检查伙伴块，如果也是 free，则不断合并为更大的块
    uint32_t rank = g_pages[blk].rank;
    for (; rank < RANK_NUM; ++rank) {
        uint32_t sib = blk ^ (1U << rank); // 伙伴块地址

        // 检查能否合并
        if ((sib < g_page_start)
        ||  (sib >= g_page_end)
        ||  (0 == g_pages[sib].head)
        ||  (rank != g_pages[sib].rank)
        ||  (PT_FREE != g_pages[sib].type)) {
            break;
        }

        // 将伙伴块从 free-list 中移除，合并为更大的块
        pglist_remove(&g_blocks[rank], sib);
        g_pages[blk | sib].head = 0; // 后一个块
        g_pages[blk & sib].rank++;   // 前一个块
        blk &= sib;
    }

    // 已经合并到最大，标记为 FREE
    g_pages[blk].type = PT_FREE;
    pglist_push_head(&g_blocks[rank], blk);
}

// 分配一个页块，起始页号必须是 N*period+phase
// 限制起始页号可以实现页面着色，优化缓存性能
static uint32_t block_alloc_nolock(uint32_t rank, uint32_t period, uint32_t phase, uint32_t type) {
    // 不断寻找大小足够的块，将更大的块拆分
    for (uint32_t rk = rank; rk < RANK_NUM; ++rk) {
        // 检查相对于这个级别 block 的偏移
        uint32_t blk_phase = phase & ~((1U << rk) - 1);

        // 遍历本层的 free blocks，寻找
    }

    return 0U;
}



// 分配页描述符数组
INIT_TEXT void page_init(size_t pa_start, size_t pa_end) {
    pa_start >>= PAGE_SHIFT;
    pa_end += PAGE_SIZE - 1;
    pa_end >>= PAGE_SHIFT;

    // 0 地址必须是无效的
    if (pa_start == 0) {
        pa_start = 1;
    }
    if (pa_end > UINT32_MAX) {
        pa_end = UINT32_MAX;
    }
    if (pa_start >= pa_end) {
        logk("fatal: no usable physical memory\n");
        return;
    }

    g_page_start = (uint32_t)pa_start;
    g_page_end = (uint32_t)pa_end;
    uint32_t page_num = g_page_end - g_page_start;
    g_pages = early_alloc_rw(page_num * sizeof(page_t));
    g_pages -= g_page_start;

    kmemset(g_pages, 0, page_num * sizeof(page_t));
    kmemset(g_blocks, 0, sizeof(g_blocks));
    // for (uint32_t i = g_page_start; i < g_page_end; ++i) {
    //     g_pages[i].prev = 0;
    // }
}

INIT_TEXT void pages_add(size_t start, size_t end) {
    start +=   PAGE_SIZE - 1;
    start &= ~(PAGE_SIZE - 1);
    end   &= ~(PAGE_SIZE - 1);

    if (start < g_page_start) {
        start = g_page_start;
    }
    if (end > g_page_end) {
        end = g_page_end;
    }

    if (start < end) {
        logk("  + usable pages 0x%lx~0x%lx\n");
    }
}
