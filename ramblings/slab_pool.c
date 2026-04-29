#include "pool.h"
#include <arch_api.h>
#include <kstring.h>
#include <debug.h>

typedef struct pool_slab {
    dlnode_t    dl;         // 链接到 pool 的 partial 或 full 链表
    vmrange_t   vm;         // 虚拟地址范围（纳入 g_kernel_vm 管理）
    void       *freelist;   // 第一个空闲对象
    uint32_t    capacity;   // 本 slab 中对象总数
    uint32_t    inuse;      // 当前已分配对象数
} pool_slab_t;

// 按 align 向上取整
static inline PURE size_t align_up(size_t x, size_t align) {
    return (x + align - 1) & ~(align - 1);
}


//------------------------------------------------------------------------------
// slab 创建与销毁
//------------------------------------------------------------------------------

static pool_slab_t *slab_create(pool_t *pool) {
    vmrange_t vm;
    size_t order = pool->slab_order;
    size_t va = vmspace_alloc(&g_kernel_vm, &vm,
        POOL_ZONE_START, POOL_ZONE_END, order, PT_KERNEL, MMU_WRITE);
    if (0 == va) {
        return NULL;
    }

    // vmrange 当前在栈上，需要拷贝到 slab 的开头并修正链表指针
    dlnode_t *p = vm.dl.prev;
    dlnode_t *n = vm.dl.next;

    pool_slab_t *slab = (pool_slab_t*)va;
    slab->vm = vm;
    p->next = &slab->vm.dl;
    n->prev = &slab->vm.dl;

    // 构建 freelist
    size_t header_size = align_up(sizeof(pool_slab_t), pool->obj_size);
    char *obj_base = (char*)va + header_size;

    slab->freelist = obj_base;
    slab->capacity = pool->objs_per_slab;
    slab->inuse = 0;

    size_t stride = pool->obj_size;
    for (uint32_t i = 0; i < slab->capacity - 1; ++i) {
        *(void**)(obj_base + i * stride) = obj_base + (i + 1) * stride;
    }
    if (slab->capacity > 0) {
        *(void**)(obj_base + (slab->capacity - 1) * stride) = NULL;
    }

    // 将物理页记录到 pool->pages
    uint32_t pfn = (uint32_t)(vm.paddr >> PAGE_SHIFT);
    pglist_push_tail(&pool->pages, pfn);

    // 插入 partial 链表
    dl_insert_after(&slab->dl, &pool->partial);

    return slab;
}

static void slab_destroy(pool_t *pool, pool_slab_t *slab) {
    uint32_t pfn = (uint32_t)(slab->vm.paddr >> PAGE_SHIFT);
    dl_remove(&slab->dl);
    pglist_remove(&pool->pages, pfn);
    // vmspace_remove 会 unmap + free 物理页
    vmspace_remove(&g_kernel_vm, &slab->vm);
}


//------------------------------------------------------------------------------
// 公共接口
//------------------------------------------------------------------------------

void pool_init(pool_t *pool, const char *name, size_t obj_size, size_t align) {
    ASSERT(NULL != pool);
    ASSERT(NULL != name);
    ASSERT(align >= sizeof(void*));
    ASSERT(0 == (align & (align - 1)));   // align 必须是 2 的幂

    // 对象大小对齐
    obj_size = align_up(obj_size, align);
    if (obj_size < sizeof(void*)) {
        obj_size = sizeof(void*);          // freelist 需要存放一个指针
    }

    // 计算 slab 参数，保证每个 slab 不少于 4 个对象
    uint32_t order = 0;
    size_t header_size = align_up(sizeof(pool_slab_t), obj_size);
    uint32_t cnt;
    do {
        size_t slab_size = PAGE_SIZE << order;
        size_t obj_area = slab_size - header_size;
        cnt = (obj_area >= obj_size) ? (uint32_t)(obj_area / obj_size) : 0;
        if (cnt >= 4) {
            break;
        }
        ++order;
    } while (order < 4);   // 最多 64K slab

    ASSERT(cnt >= 4);

    pool->lock = SPIN_INIT;
    dl_init_circular(&pool->partial);
    dl_init_circular(&pool->full);
    kmemset(&pool->pages, 0, sizeof(pglist_t));
    pool->name = name;
    pool->obj_size = obj_size;
    pool->objs_per_slab = (uint16_t)cnt;
    pool->slab_order = (uint16_t)order;
    pool->allocs = 0;
    pool->frees = 0;
}

MALLOC void *pool_alloc(pool_t *pool) {
    int key = irq_spin_take(&pool->lock);

    // 找第一个有空闲的 slab
    if (pool->partial.next == &pool->partial) {
        // 没有空闲 slab，分配新的
        pool_slab_t *slab = slab_create(pool);
        if (NULL == slab) {
            irq_spin_give(&pool->lock, key);
            panic("pool \"%s\": out of memory", pool->name);
        }
    }

    pool_slab_t *slab = containerof(pool->partial.next, pool_slab_t, dl);
    ASSERT(slab->inuse < slab->capacity);
    ASSERT(NULL != slab->freelist);

    void *obj = slab->freelist;
    slab->freelist = *(void**)obj;
    ++slab->inuse;
    ++pool->allocs;

    // 如果 slab 现在满了，移到 full 链表
    if (slab->inuse == slab->capacity) {
        dl_remove(&slab->dl);
        dl_insert_after(&slab->dl, &pool->full);
    }

    irq_spin_give(&pool->lock, key);
    return obj;
}

void pool_free(pool_t *pool, void *ptr) {
    ASSERT(NULL != ptr);

    int key = irq_spin_take(&pool->lock);

    // 通过 vmspace_find 找到所属 slab
    vmrange_t *vm = vmspace_find(&g_kernel_vm, (size_t)ptr);
    ASSERT(NULL != vm);
    ASSERT(vm->paddr != 0);

    pool_slab_t *slab = containerof(vm, pool_slab_t, vm);
    ASSERT(slab->inuse > 0);

    uint32_t was_full = (slab->inuse == slab->capacity);

    // 将对象放回 freelist
    *(void**)ptr = slab->freelist;
    slab->freelist = ptr;
    --slab->inuse;
    ++pool->frees;

    if (0 == slab->inuse) {
        // 整个 slab 空闲，归还系统
        slab_destroy(pool, slab);
    } else if (was_full) {
        // 从全满变部分满，移回 partial
        dl_remove(&slab->dl);
        dl_insert_after(&slab->dl, &pool->partial);
    }

    irq_spin_give(&pool->lock, key);
}

void pool_destroy(pool_t *pool) {
    int key = irq_spin_take(&pool->lock);

    // 收集所有 slab
    while (pool->partial.next != &pool->partial) {
        pool_slab_t *slab = containerof(pool->partial.next, pool_slab_t, dl);
        slab_destroy(pool, slab);
    }
    while (pool->full.next != &pool->full) {
        pool_slab_t *slab = containerof(pool->full.next, pool_slab_t, dl);
        slab_destroy(pool, slab);
    }

    irq_spin_give(&pool->lock, key);
}
