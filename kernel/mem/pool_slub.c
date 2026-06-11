#include "pool_slub.h"
#include <vmspace.h>
#include <arch_api.h>
#include <debug.h>



// 基于 SLUB 实现的内存池，可以实现相同大小对象的高效分配回收

// 一个 pool 包括很多个 block（大小为 2^slab_order 个页），划分为若干 object
// object 大小是 2 的幂，紧凑排列
// object 有已分配和未分配两种状态，未分配对象也称 free-object，组成一个链表（freelist）


// 一个内存池包含多个 slab，一个 slab 就是一个页
// 每个 slab 划分为若干固定大小的 object，free-object 组成单链表
// free-object 开头两字节就是 next-free，不是指针，而是相对 slab 的偏移量
// slab 只有一个页，因此使用 uint16 表示偏移量就足够了
// 页描述符里面，objects 指向第一个 free-object

// slab 按占用情况分属三个链表：
//  - empty   : 全部未分配，ent_num==0
//  - partial : 部分分配，按 ent_num 升序排列
//  - full    : 全部已分配，ent_num==MAX
// 也可以认为这是一个链表，只是分成了三个子序列，提供跳表可以快速索引
//
// 分配时优先从 partial 头部（空闲最多）取，释放时按 ent_num 重排
// 链表以维持这个顺序。empty 和 full 链表不分顺序。




#define NO_OBJ 0xFFFFU

static inline size_t align_up(size_t x, size_t align) {
    return (x + align - 1) & ~(align - 1);
}

//------------------------------------------------------------------------------
// slab 级别：对象分配与释放
//------------------------------------------------------------------------------

static uint32_t slab_create(size_t obj_size) {
    size_t pa = page_alloc(0, PT_POOL);
    if (0 == pa) {
        return 0;
    }

    size_t va = GUARDED_IDMAP_ADDR + pa * 2;
    mmu_map(g_kernel_vm.table, va, va + PAGE_SIZE, pa, MMU_WRITE);

    // page_alloc 已正确设置 head / rank / type，
    // 但 ent_num / objects 可能残留旧值，显式初始化
    uint32_t pfn = (uint32_t)(pa >> PAGE_SHIFT);
    g_pages[pfn].ent_num = 0;
    g_pages[pfn].objects = 0; // 指向第一个 object

    // 嵌入式 freelist：每个对象开头 2 字节存下一个对象偏移
    uint32_t obj_count = (uint32_t)(PAGE_SIZE / obj_size);
    for (uint32_t i = 1, off = obj_size; i < obj_count; ++i) {
        *(uint16_t*)va = off;
        va += obj_size;
        off += obj_size;
    }
    *(uint16_t*)va = NO_OBJ;

    return pfn;
}

static void slab_release(uint32_t slab) {
    size_t va = GUARDED_IDMAP_ADDR + (slab << (PAGE_SHIFT + 1));
    tlb_shootdown(va, va + PAGE_SIZE);
    mmu_unmap(g_kernel_vm.table, va, va + PAGE_SIZE);
    page_free((size_t)slab << PAGE_SHIFT);
}

static void *slab_obj_alloc(uint32_t slab, size_t obj_size UNUSED) {
    ASSERT(PT_POOL == g_pages[slab].type);
    ASSERT(g_pages[slab].head);
    ASSERT(NO_OBJ != g_pages[slab].objects);

    // char *base = (char*)pfn_to_virt(slab);
    size_t base = GUARDED_IDMAP_ADDR + ((size_t)slab << (PAGE_SHIFT + 1));
    uint16_t off = g_pages[slab].objects;
    g_pages[slab].objects = *(uint16_t*)(base + off);
    g_pages[slab].ent_num += 1;

    return (void*)(base + off);
}

static void slab_obj_free(uint32_t slab, void *obj) {
    ASSERT(PT_POOL == g_pages[slab].type);
    ASSERT(g_pages[slab].head);
    ASSERT(0 != g_pages[slab].ent_num);

    // char *base = (char*)pfn_to_virt(slab);
    size_t base = GUARDED_IDMAP_ADDR + ((size_t)slab << (PAGE_SHIFT + 1));
    uint16_t head = g_pages[slab].objects;
    *(uint16_t*)obj = head;
    g_pages[slab].objects = (uint16_t)((size_t)obj - base);
    g_pages[slab].ent_num -= 1;
}

//------------------------------------------------------------------------------
// 缓存级别：slub 初始化与销毁
//------------------------------------------------------------------------------

void pool_init(pool_t *slub, size_t obj_size) {
    obj_size = align_up(obj_size, arch_cacheline_size());

    // // 寻找能容纳至少 8 个对象的最小 slab 阶数
    // uint32_t order = 0;
    // for (; order < PAGE_BLOCK_RANK_NUM; ++order) {
    //     if ((8 * obj_size) <= ((size_t)PAGE_SIZE << order)) {
    //         break;
    //     }
    // }
    // ASSERT(order < PAGE_BLOCK_RANK_NUM);

    // slub->lock       = SPINLOCK_INIT;
    slub->obj_size   = (uint16_t)obj_size;
    // slub->slab_order = (uint8_t)order;
    slub->empty      = (pglist_t){0, 0};
    slub->partial    = (pglist_t){0, 0};
    slub->full       = (pglist_t){0, 0};
}

void pool_destroy_nolock(pool_t *slub) {
    // SPINLOCK_SCOPED(&slub->lock);
    uint32_t pfn;
    while (0 != (pfn = slub->empty.head)) {
        pglist_remove(&slub->empty, pfn);
        slab_release(pfn);
    }
    while (0 != (pfn = slub->partial.head)) {
        pglist_remove(&slub->partial, pfn);
        slab_release(pfn);
    }
    while (0 != (pfn = slub->full.head)) {
        pglist_remove(&slub->full, pfn);
        slab_release(pfn);
    }
}

void pool_shrink_nolock(pool_t *slub) {
    // SPINLOCK_SCOPED(&slub->lock);
    uint32_t pfn;
    while (0 != (pfn = slub->full.head)) {
        pglist_remove(&slub->full, pfn);
        slab_release(pfn);
    }
}

//------------------------------------------------------------------------------
// 缓存级别：对象分配与释放
//------------------------------------------------------------------------------

void *pool_alloc_nolock(pool_t *slub) {
    // SPINLOCK_SCOPED(&slub->lock);

    // partial 为空时，从 empty 取 slab 或创建新的
    if (0 == slub->partial.head) {
        uint32_t pfn;
        if (0 != slub->empty.head) {
            pfn = slub->empty.head;
            pglist_remove(&slub->empty, pfn);
        } else {
            pfn = slab_create(slub->obj_size);
            if (0 == pfn) {
                return NULL;
            }
        }
        pglist_push_tail(&slub->partial, pfn);
    }

    // 从 partial 尾部取 slab — 尾部的 slab 空闲最少，
    // 优先将其填满，以将空闲集中在更靠头部的位置
    uint32_t pfn = slub->partial.tail;
    void *obj = slab_obj_alloc(pfn, slub->obj_size);

    // slab 已满 → full
    if (NO_OBJ == g_pages[pfn].objects) {
        pglist_remove(&slub->partial, pfn);
        pglist_push_head(&slub->full, pfn);
    }

    return obj;
}

void pool_free_nolock(pool_t *slub, void *obj) {
    // SPINLOCK_SCOPED(&slub->lock);

    // uint32_t pfn = page_block_head(virt_to_pfn(obj));
    uint32_t objpg = ((size_t)obj - GUARDED_IDMAP_ADDR) >> (PAGE_SHIFT + 1);
    uint32_t pfn = page_block_head(objpg);
    uint32_t was_full = (NO_OBJ == g_pages[pfn].objects);
    slab_obj_free(pfn, obj);
    uint32_t inuse = g_pages[pfn].ent_num;

    // 完全空闲 → empty
    if (0 == inuse) {
        pglist_remove(&slub->partial, pfn);
        pglist_push_head(&slub->empty, pfn);
        return;
    }

    // 之前是 full（无空闲对象），现在有空闲对象 → partial
    if (was_full) {
        pglist_remove(&slub->full, pfn);
        pglist_push_tail(&slub->partial, pfn);
        return;
    }

    // 仍在 partial 中 — 按 inuse 升序前移（空闲越多越靠头部）
    {
        uint32_t prev = g_pages[pfn].prev;
        while (0 != prev && inuse < g_pages[prev].ent_num) {
            prev = g_pages[prev].prev;
        }

        // 位置已正确，无需移动
        if (prev == g_pages[pfn].prev) {
            return;
        }

        pglist_remove(&slub->partial, pfn);
        if (0 == prev) {
            pglist_push_head(&slub->partial, pfn);
        } else {
            uint32_t next = g_pages[prev].next;
            g_pages[pfn].prev = prev;
            g_pages[pfn].next = next;
            g_pages[prev].next = pfn;
            if (0 != next) {
                g_pages[next].prev = pfn;
            } else {
                slub->partial.tail = pfn;
            }
        }
    }
}
