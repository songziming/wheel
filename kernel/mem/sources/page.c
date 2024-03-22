// 物理页面管理/分配

#include <page.h>
#include <wheel.h>
#include <spin.h>
#include <shell.h>


// TODO 除了管理物理页，还要记录页面颜色直方图
//      提供 first-level-cache 和 last-level-cache 的颜色数量

// 页描述符
typedef struct page {
    pfn_t prev;
    pfn_t next;

    uint32_t head : 1;   // 是不是块中第一个页
    uint32_t rank : 4;   // 所在块的大小，head==1 才有效
    uint32_t type : 4;   // 所在块的类型，head==1 才有效

    page_info_t info;
} page_t;



// 全局页描述符数组，每个物理页面都对应一个结构体
static CONST pfn_t g_page_num = 0;
static CONST page_t *g_pages = NULL;

// 未分配的页块按不同大小组成链表
#define RANK_NUM 16
static pglist_t g_blocks[RANK_NUM];

static spin_t g_pages_lock = SPIN_INIT;
static shell_cmd_t g_cmd_page;



//------------------------------------------------------------------------------
// 连续的物理页构成块，多个页块组成链表
//------------------------------------------------------------------------------

// inline pfn_t prev_page_block(pfn_t blk) {
//     ASSERT(INVALID_PFN != blk);
//     return g_pages[blk].head ? g_pages[blk].prev : INVALID_PFN;
// }

// inline pfn_t next_page_block(pfn_t blk) {
//     ASSERT(INVALID_PFN != blk);
//     return g_pages[blk].head ? g_pages[blk].next : INVALID_PFN;
// }

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
// 相同类型的页块可以组成链表
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
        pglist_remove(&g_blocks[rank], sib);
        blk = block_merge(blk, sib);
    }

    // 已经合并到最大，标记为 FREE
    // 将合并之后的 block 添加到链表（的开头）
    g_pages[blk].type = PT_FREE;
    pglist_push_head(&g_blocks[rank], blk);
}

// 申请一个页块，起始页号必须是 N*period+phase
// 限制起始页号可以实现页面着色，优化缓存性能
static pfn_t block_alloc(uint8_t rank, pfn_t period, pfn_t phase, page_type_t type) {
    ASSERT(rank < RANK_NUM);
    ASSERT(0 == (period & (period - 1)));
    ASSERT(phase < period);
    ASSERT(PT_INVALID != type);
    ASSERT(PT_FREE != type);

    pfn_t size = 1 << rank;
    ASSERT(0 == (phase & (size - 1)));

    for (uint8_t blk_rank = rank; blk_rank < RANK_NUM; ++blk_rank, size <<= 1) {
        pfn_t blk_phase = phase & ~(size - 1); // phase 相对于当前 rank 的偏移量

        // 遍历本层的 free block，寻找目标 color
        pfn_t blk = g_blocks[blk_rank].head;
        for (; INVALID_PFN != blk; blk = g_pages[blk].next) {
            if ((blk & (period - 1)) == blk_phase) {
                break;
            }
        }

        if (INVALID_PFN == blk) {
            continue;
        }

        // 将这个 block 标记为已分配
        pglist_remove(&g_blocks[blk_rank], blk);
        g_pages[blk].type = type;

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

// TODO 改名为 strtoul，放在 str.c，定义为若符号，避免与 libc 冲突
static uint64_t strtou64(const char *s) {
    ASSERT(NULL != s);

    int base = 10;
    if ('0' == *s) {
        ++s;
        base = 8;
        if (('x' == *s) || ('X' == *s)) {
            ++s;
            base = 16;
        }
    }

    uint64_t num = 0;
    for (; *s; ++s) {
        num *= base;
        if (('0' <= *s) && (*s <= '9')) {
            num += *s - '0';
        } else if (('a' <= *s) && (*s <= 'f')) {
            num += *s - 'a' + 10;
        } else if (('A' <= *s) && (*s <= 'F')) {
            num += *s - 'A' + 10;
        } else {
            break;
        }
    }

    return num;
}

static int page_show(int argc, char *argv[]) {
    if (2 == argc) {
        uint64_t addr = strtou64(argv[1]);
        size_t pn = addr >> PAGE_SHIFT;

        if (pn >= g_page_num) {
            klog("invalid phys_addr: 0x%lx\n", addr);
            return 1;
        }

        pfn_t blk = page_block_head(pn);
        klog("addr %lx, block %x, size %x, type %d\n",
            addr, blk, 1 << g_pages[blk].rank, g_pages[blk].type);
        return 0;
    }

    int key = irq_spin_take(&g_pages_lock);

    klog("free pages:\n");
    for (pfn_t blk = 0; blk < g_page_num;) {
        pfn_t size = page_block_size(blk);
        if (PT_FREE == g_pages[blk].type) {
            klog("  - block 0x%x, size 0x%x\n", blk, size);
        }
        blk += size;
    }

    irq_spin_give(&g_pages_lock, key);
    return 0;
}


// 传入物理内存的上限
INIT_TEXT void page_init(size_t end) {
    ASSERT(0 == g_page_num);
    ASSERT(NULL == g_pages);

    end >>= PAGE_SHIFT;

    if (end > INVALID_PFN) {
        end = INVALID_PFN;
    }
    g_page_num = (pfn_t)end;
    g_pages = (page_t *)early_alloc_rw(g_page_num * sizeof(page_t));

    for (pfn_t i = 0; i < g_page_num; ++i) {
        g_pages[i].prev = INVALID_PFN;
        g_pages[i].next = INVALID_PFN;
        g_pages[i].type = PT_INVALID;
        g_pages[i].head = 1;
        g_pages[i].rank = 0;
    }

    for (int i = 0; i < RANK_NUM; ++i) {
        g_blocks[i].head = INVALID_PFN;
        g_blocks[i].tail = INVALID_PFN;
    }

    // 注册打印物理页的命令
    g_cmd_page.name = "page";
    g_cmd_page.func = page_show;
    shell_add_cmd(&g_cmd_page);
}

// 将一段内存标记为有效内存
INIT_TEXT void page_add(size_t start, size_t end, page_type_t type) {
    ASSERT(0 != g_page_num);
    ASSERT(NULL != g_pages);
    ASSERT(0 == (start & (PAGE_SIZE - 1)));
    ASSERT(0 == (end & (PAGE_SIZE - 1)));

    start = (start + PAGE_SIZE - 1) >> PAGE_SHIFT;
    end = end >> PAGE_SHIFT;

    if (end > g_page_num) {
        end = g_page_num;
    }
    if (start >= end) {
        return;
    }
    for (size_t i = start; i < end; ++i) {
        ASSERT(g_pages[i].type != type);
        g_pages[i].head = 0;
    }

    // 这一段内存不一定是按块对齐的，尽可能使用更大的块
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
        g_pages[start].type = type;
        if (PT_FREE == type) {
            block_free(start);
        }
        start += (1UL << rank);
    }
}

// 申请一个物理页块
size_t pages_alloc(int rank, page_type_t type) {
    ASSERT(rank >= 0);
    ASSERT(rank < RANK_NUM);
    ASSERT(PT_INVALID != type);
    ASSERT(PT_FREE != type);

    int key = irq_spin_take(&g_pages_lock);
    pfn_t pg = block_alloc(rank, 1, 0, type);
    irq_spin_give(&g_pages_lock, key);

    if (INVALID_PFN == pg) {
        return INVALID_ADDR;
    }
    return (size_t)pg << PAGE_SHIFT;
}

// 回收一个物理页块
void pages_free(size_t pa) {
    ASSERT(0 == (pa & (PAGE_SIZE - 1)));

    pa >>= PAGE_SHIFT;
    ASSERT(pa < g_page_num);
    ASSERT(g_pages[pa].head);
    ASSERT(PT_FREE != g_pages[pa].type);

    int key = irq_spin_take(&g_pages_lock);
    block_free((pfn_t)pa);
    irq_spin_give(&g_pages_lock, key);
}

#if 0
// 将一段内核使用的物理内存回收（用于回收 init）
void reclaim_kernel_pages(size_t pa, size_t end) {
    ASSERT(0 == (pa  & (PAGE_SIZE - 1)));
    ASSERT(0 == (end & (PAGE_SIZE - 1)));

    pa  >>= PAGE_SHIFT;
    end >>= PAGE_SHIFT;
    ASSERT(pa < end);
    ASSERT(end <= g_page_num);

    int key = irq_spin_take(&g_pages_lock);

    for (; pa < end; ++pa) {
        ASSERT(0 == g_pages[pa].rank);
        ASSERT(g_pages[pa].head);
        ASSERT(PT_KERNEL == g_pages[pa].type);
        block_free((pfn_t)pa);
    }

    irq_spin_give(&g_pages_lock, key);
}
#endif
