#include "page.h"
#include "early_alloc.h"
#include <arch_api.h>
#include <kstring.h>
#include <spin.h>
#include <debug.h>

#include <kshell.h>
#include <console.h>


CONST uint32_t g_page_start;
CONST uint32_t g_page_end;
CONST page_t *g_pages;

static spin_t g_page_spin = SPIN_INIT;
static pglist_t g_blocks[PAGE_BLOCK_RANK_NUM];




//------------------------------------------------------------------------------
// 页块链表
//------------------------------------------------------------------------------

// 从任意 pfn 向上找到所在 block 的头页
uint32_t page_block_head(uint32_t pfn) {
    while (!g_pages[pfn].head) --pfn;
    return pfn;
}

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
    for (; rank < PAGE_BLOCK_RANK_NUM - 1; ++rank) {
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
static uint32_t block_alloc_nolock(uint32_t rank, uint32_t period, uint32_t phase, page_type_t type) {
    ASSERT(type > PT_FREE);
    ASSERT(0 == (period & (period - 1)));   // period 必须是 2 的幂
    ASSERT(phase == (phase & (period - 1))); // phase 必须小于 period
    ASSERT(0 == (phase & ((1U << rank) - 1))); // phase 必须是 rank 的倍数

    // 不断寻找大小足够的块，将更大的块拆分
    uint32_t blk_rank;
    uint32_t blk;
    for (blk_rank = rank; blk_rank < PAGE_BLOCK_RANK_NUM; ++blk_rank) {
        uint32_t color = phase & ~((1U << blk_rank) - 1);   // 相对于这个级别 block 的偏移

        // 遍历本层的 free blocks，寻找起始地址符合要求的块
        blk = g_blocks[blk_rank].head;
        for (; blk; blk = g_pages[blk].next) {
            if ((blk & (period - 1)) == color) {
                goto found;
            }
        }
    }
    return 0U;

found:
    pglist_remove(&g_blocks[blk_rank], blk);
    g_pages[blk].type = type; // 标记为已分配

    // 如果这个块超过所需，则将 block 分割为两个子块，返回不需要的部分
    // 根据 phase 决定每一级回收前一半还是后一半
    while (blk_rank > rank) {
        --blk_rank;
        uint32_t sib = blk + (1U << blk_rank);
        g_pages[blk].rank = blk_rank;
        g_pages[sib].head = 1;
        g_pages[sib].rank = blk_rank;
        g_pages[sib].type = type;

        if ((blk ^ sib) & phase) {
            block_free_nolock(blk); // 回收前一半，保留后一半
            blk = sib;
        } else {
            block_free_nolock(sib); // 回收后一半，保留前一半
        }
    }

    return blk;
}

// 分配若干不连续的物理页
static void pagelist_alloc_nolock(pglist_t *pl, uint32_t num, page_type_t type) {
    uint32_t rank;
    uint32_t size;
    uint32_t blk;

    // TODO 首先检查剩余 page 数量是否满足
    //      如果剩余内存太少则直接退出

    for (rank = 0; rank < PAGE_BLOCK_RANK_NUM; ++rank) {
        size = 1U << rank;

        // 不断从队列中取页块
        while (0 != (blk = g_blocks[rank].head)) {
            pglist_remove(&g_blocks[rank], blk);
            if (num < size) {
                // 页块超过所需，将其拆开
                // 此时 blk 页类型还是 FREE
                goto split_into_smaller;
            }
            num -= size;
            g_pages[blk].type = type; // 标记为已分配
            pglist_push_tail(pl, blk);
            if (0 == num) {
                return;
            }
        }
    }

    if ((0 == blk) || (num > size)) {
        logk("warning: cannot satisfy required pages\n");
        return;
    }

    ASSERT(0 != blk);
    ASSERT(num < size);

split_into_smaller:
    // 当前 blk 超过了所需的大小 num，需要拆分，将一部分放回
    // 拆解为 size/2、size/4、size/8、...、4、2、1
    // 从里面挑选某些 block 放入链表
    while (rank > 0) {
        --rank;
        size >>= 1;
        g_pages[blk].rank = rank;
        uint32_t sib = blk + size;
        g_pages[sib].head = 1;
        g_pages[sib].rank = rank;
        g_pages[sib].type = g_pages[blk].type;

        if (num >= size) {
            num -= size;
            g_pages[sib].type = type;
            pglist_push_head(pl, sib);
        } else {
            pglist_push_head(&g_blocks[rank], sib);
        }

        if (0 == num) {
            pglist_push_tail(&g_blocks[rank], blk);
            return;
        }
    }

    logk("error: cannot allocate all required pages\n");
}


//------------------------------------------------------------------------------
// public functions
//------------------------------------------------------------------------------

size_t page_alloc_color(uint32_t rank, page_type_t type,
        uint32_t period, uint32_t phase) {
    int key = irq_spin_take(&g_page_spin);
    uint32_t blk = block_alloc_nolock(rank, period, phase, type);
    irq_spin_give(&g_page_spin, key);
    return (size_t)blk << PAGE_SHIFT;
}

size_t page_alloc(uint32_t rank, page_type_t type) {
    return page_alloc_color(rank, type, 1, 0);
}

void page_free(size_t pa) {
    int key = irq_spin_take(&g_page_spin);
    uint32_t blk = (uint32_t)(pa >> PAGE_SHIFT);
    block_free_nolock(blk);
    irq_spin_give(&g_page_spin, key);
}


void pagelist_alloc(pglist_t *pl, uint32_t num, page_type_t type) {
    int key = irq_spin_take(&g_page_spin);
    pagelist_alloc_nolock(pl, num, type);
    irq_spin_give(&g_page_spin, key);
}

void pagelist_free(pglist_t *pl) {
    int key = irq_spin_take(&g_page_spin);

    for (uint32_t blk = pl->head; blk; ) {
        uint32_t next = g_pages[blk].next; // 必须先得到后继页块
        block_free_nolock(blk); // 这会修改 g_pages
        blk = next;
    }

    irq_spin_give(&g_page_spin, key);
    pl->head = 0;
    pl->tail = 0;
}


uint32_t page_free_count() {
    int key = irq_spin_take(&g_page_spin);
    uint32_t npages = 0;
    for (int rank = 0; rank < PAGE_BLOCK_RANK_NUM; ++rank) {
        uint32_t blksize = 1U << rank;
        for (uint32_t pfn = g_blocks[rank].head; 0 != pfn; pfn = g_pages[pfn].next) {
            npages += blksize;
        }
    }
    irq_spin_give(&g_page_spin, key);
    return npages;
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
    kmemset(g_pages, 0, page_num * sizeof(page_t));
    g_pages -= g_page_start;

    kmemset(g_blocks, 0, sizeof(g_blocks));
}

INIT_TEXT void pages_add(size_t start, size_t end) {
    start += PAGE_SIZE - 1;
    start >>= PAGE_SHIFT;
    end   >>= PAGE_SHIFT;

    if (start < g_page_start) {
        start = g_page_start;
    }
    if (end > g_page_end) {
        end = g_page_end;
    }
    if (start >= end) {
        return;
    }

    int key = irq_spin_take(&g_page_spin);

    // 这一段内存不一定是按块对齐的，尽可能使用更大的块
    while (start < end) {
        int rank = __builtin_ctz(start);
        if (rank >= PAGE_BLOCK_RANK_NUM) {
            rank = PAGE_BLOCK_RANK_NUM - 1;
        }

        uint32_t size = 1U << rank;
        while (start + size > end) {
            size >>= 1;
            rank--;
        }

        // 创建一个块，并将其回收
        g_pages[start].head = 1;
        g_pages[start].rank = rank;
        block_free_nolock(start);
        start += size;
    }

    irq_spin_give(&g_page_spin, key);
}

//------------------------------------------------------------------------------

#if !defined(UNIT_TEST)

static void show_page(int argc, char *argv[]) {
    if (argc < 2) {
        console_printf("usage: %s PFN\n", argv[0]);
        return;
    }

    size_t pfn = str2num(argv[1]);
    uint32_t blk = page_block_head(pfn);
    uint32_t rank = g_pages[blk].rank;
    uint32_t end = blk + (1U << rank);
    console_printf("blk=0x%x(%u), rank=%u, end=0x%x(%u) type=%u\n",
        blk, blk, rank, end, end, g_pages[blk].type);
}

KSHELL_CMD("page", show_page);

#endif // !defined(UNIT_TEST)
