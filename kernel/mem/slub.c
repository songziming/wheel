#include "slub.h"
#include <arch_api.h>
#include <debug.h>

// 单元测试，模仿虚拟地址和物理地址的转换
#if defined(UNIT_TEST)
extern uint64_t g_direct_map_base;
#undef DIRECT_MAP_ADDR
#define DIRECT_MAP_ADDR g_direct_map_base
#endif

// 基于 SLUB 实现的内存池，可以实现相同大小对象的高效分配回收

// 一个 pool 包括很多个 block（大小为 2^slab_order 个页），划分为若干 object
// object 大小是 2 的幂，紧凑排列
// object 有已分配和未分配两种状态，未分配对象也称 free-object，组成一个链表（freelist）

// SLAB 将元数据保存在 block 开头，这就破坏了 object 布局
// SLUB 将元数据保存在 page_t 里面，对象布局更整齐
// 为节省空间，freelist 不使用指针，而是用 object 编号，uint16 就够了

// slab 按占用情况分属三个链表：
//  - empty   : 全部未分配，ent_num==0
//  - partial : 部分分配，按 ent_num 升序排列
//  - full    : 全部已分配，ent_num==MAX
// 也可以认为这是一个链表，只是分成了三个子序列，提供跳表可以快速索引
//
// 分配时优先从 partial 头部（空闲最多）取，释放时按 ent_num 重排
// 链表以维持这个顺序。empty 和 full 链表不分顺序。


// SLUB 元数据保存在 page_t，容不下复杂的信息
// 所以只能直接映射在 direct-map 区域，无法动态分配虚拟地址，也无法实现 guard page


#define NO_OBJ          0xFFFFU

static inline size_t align_up(size_t x, size_t align) {
    return (x + align - 1) & ~(align - 1);
}

static inline void *pfn_to_virt(uint32_t pfn) {
    return (void *)(DIRECT_MAP_ADDR + ((size_t)pfn << PAGE_SHIFT));
}

static inline uint32_t virt_to_pfn(void *va) {
    return (uint32_t)(((size_t)va - DIRECT_MAP_ADDR) >> PAGE_SHIFT);
}

//------------------------------------------------------------------------------
// slab 级别：对象分配与释放
//------------------------------------------------------------------------------

static uint32_t slab_create(uint8_t order, size_t obj_size) {
    size_t pa = PAGE_ALLOC(order, PT_POOL);
    if (0 == pa) {
        return 0;
    }

    uint32_t pfn = (uint32_t)(pa >> PAGE_SHIFT);
    void *addr = pfn_to_virt(pfn);

    // page_alloc 已正确设置 head / rank / type，
    // 但 ent_num / objects 可能残留旧值，显式初始化
    g_pages[pfn].ent_num = 0;
    g_pages[pfn].objects = 0;

    // 嵌入式 freelist：每个对象开头 2 字节存下一个对象偏移
    size_t slab_size = PAGE_SIZE << order;
    uint32_t cnt = (uint32_t)(slab_size / obj_size);
    for (uint32_t i = 0; i < cnt; ++i) {
        uint16_t next = (i + 1 < cnt) ? (uint16_t)((i + 1) * obj_size) : NO_OBJ;
        *(uint16_t *)((char *)addr + i * obj_size) = next;
    }

    return pfn;
}

static void *slab_obj_alloc(uint32_t slab, size_t obj_size UNUSED) {
    ASSERT(PT_POOL == g_pages[slab].type);
    ASSERT(g_pages[slab].head);
    ASSERT(NO_OBJ != g_pages[slab].objects);

    char *base = (char *)pfn_to_virt(slab);
    uint16_t off = g_pages[slab].objects;
    g_pages[slab].objects = *(uint16_t *)(base + off);
    g_pages[slab].ent_num += 1;

    return base + off;
}

static void slab_obj_free(uint32_t slab, void *obj) {
    ASSERT(PT_POOL == g_pages[slab].type);
    ASSERT(g_pages[slab].head);
    ASSERT(0 != g_pages[slab].ent_num);

    char *base = (char *)pfn_to_virt(slab);
    uint16_t head = g_pages[slab].objects;
    *(uint16_t *)obj = head;
    g_pages[slab].objects = (uint16_t)((char *)obj - base);
    g_pages[slab].ent_num -= 1;
}

//------------------------------------------------------------------------------
// 缓存级别：slub 初始化与销毁
//------------------------------------------------------------------------------

void slub_init(slub_t *slub, size_t obj_size) {
    obj_size = align_up(obj_size, arch_cacheline_size());

    // 寻找能容纳至少 8 个对象的最小 slab 阶数
    uint32_t order = 0;
    for (; order < PAGE_BLOCK_RANK_NUM; ++order) {
        if ((8 * obj_size) <= (PAGE_SIZE << order)) {
            break;
        }
    }
    ASSERT(order < PAGE_BLOCK_RANK_NUM);

    slub->lock       = SPIN_INIT;
    slub->obj_size   = (uint16_t)obj_size;
    slub->slab_order = (uint8_t)order;
    slub->empty      = (pglist_t){0, 0};
    slub->partial    = (pglist_t){0, 0};
    slub->full       = (pglist_t){0, 0};
}

void slub_destroy(slub_t *slub) {
    raw_spin_take(&slub->lock);

    uint32_t pfn;
    while (0 != (pfn = slub->empty.head)) {
        pglist_remove(&slub->empty, pfn);
        page_free((size_t)pfn << PAGE_SHIFT);
    }
    while (0 != (pfn = slub->partial.head)) {
        pglist_remove(&slub->partial, pfn);
        page_free((size_t)pfn << PAGE_SHIFT);
    }
    while (0 != (pfn = slub->full.head)) {
        pglist_remove(&slub->full, pfn);
        page_free((size_t)pfn << PAGE_SHIFT);
    }

    raw_spin_give(&slub->lock);
}

void slub_shrink(slub_t *slub) {
    raw_spin_take(&slub->lock);

    uint32_t pfn;
    while (0 != (pfn = slub->full.head)) {
        pglist_remove(&slub->full, pfn);
        page_free((size_t)pfn << PAGE_SHIFT);
    }

    raw_spin_give(&slub->lock);
}

//------------------------------------------------------------------------------
// 缓存级别：对象分配与释放
//------------------------------------------------------------------------------

void *slub_alloc(slub_t *slub) {
    raw_spin_take(&slub->lock);

    // partial 为空时，从 empty 取 slab 或创建新的
    if (0 == slub->partial.head) {
        uint32_t pfn;
        if (0 != slub->empty.head) {
            pfn = slub->empty.head;
            pglist_remove(&slub->empty, pfn);
        } else {
            pfn = slab_create(slub->slab_order, slub->obj_size);
            if (0 == pfn) {
                raw_spin_give(&slub->lock);
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

    raw_spin_give(&slub->lock);
    return obj;
}

void slub_free(slub_t *slub, void *obj) {
    raw_spin_take(&slub->lock);

    uint32_t pfn = page_block_head(virt_to_pfn(obj));
    uint32_t was_full = (NO_OBJ == g_pages[pfn].objects);
    slab_obj_free(pfn, obj);
    uint32_t inuse = g_pages[pfn].ent_num;

    // 完全空闲 → empty
    if (0 == inuse) {
        pglist_remove(&slub->partial, pfn);
        pglist_push_head(&slub->empty, pfn);
        raw_spin_give(&slub->lock);
        return;
    }

    // 之前是 full（无空闲对象），现在有空闲对象 → partial
    if (was_full) {
        pglist_remove(&slub->full, pfn);
        pglist_push_tail(&slub->partial, pfn);
        raw_spin_give(&slub->lock);
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
            raw_spin_give(&slub->lock);
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

    raw_spin_give(&slub->lock);
}
