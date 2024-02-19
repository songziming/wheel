// 管理一套地址空间，包括 vm 链表和分页表

#include <context.h>
#include <wheel.h>
#include <spin.h>


static context_t g_kernel_ctx;


//------------------------------------------------------------------------------
// 虚拟地址空间管理，不涉及锁，不涉及页表
//------------------------------------------------------------------------------

// 寻找一段可分配的虚拟地址范围，前后留出 guard page
static size_t vm_search(context_t *ctx, size_t start, size_t end, size_t size) {
    ASSERT(NULL != ctx);
    ASSERT(ctx->spin.ticket_counter > ctx->spin.service_counter);

    // 如果 vmspace 为空，则无需关心 guard pages
    if (dl_is_lastone(&ctx->head) && (start + size <= end)) {
        return start;
    }

    // 从前到后顺序遍历，遇到第一个满足大小要求的空间就返回
    size_t prev_end = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (dlnode_t *i = ctx->head.next; &ctx->head!= i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        size_t rng_start = rng->addr & ~(PAGE_SIZE - 1);
        size_t rng_end = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        // 无条件留出 guard page（可以通过 flag 控制）
        rng_start -= PAGE_SIZE;
        rng_end += PAGE_SIZE;

        if (rng_end < prev_end) {
            continue;
        }

        if ((prev_end + size <= rng_start) && (prev_end + size <= end)) {
            return prev_end;
        }

        prev_end = rng_end;

        if (prev_end >= end) {
            break;
        }
    }

    // 还要检查最后一段范围
    if (prev_end + size <= end) {
        return prev_end;
    }

    return INVALID_ADDR;
}

// 将一段虚拟地址范围注册到地址空间中
static void vm_insert(context_t *ctx, vmrange_t *rng) {
    ASSERT(NULL != ctx);
    ASSERT(ctx->spin.ticket_counter > ctx->spin.service_counter);
    ASSERT(NULL != rng);
    ASSERT(rng->addr < rng->end);
    ASSERT(!dl_contains(&ctx->head, &rng->dl));

    // 在链表中寻找一个位置
    dlnode_t *node = ctx->head.next;
    for (; &ctx->head != node; node = node->next) {
        vmrange_t *cur = containerof(node, vmrange_t, dl);
        if (cur->addr <= rng->addr) {
            ASSERT(cur->addr < cur->end);
            ASSERT(cur->end <= rng->addr);
            continue;
        }

        ASSERT(cur->addr >= rng->end);
        break;
    }

    dl_insert_before(&rng->dl, node);
}

static void vm_remove(context_t *ctx, vmrange_t *rng) {
    ASSERT(NULL != ctx);
    ASSERT(ctx->spin.ticket_counter > ctx->spin.service_counter);
    ASSERT(NULL != rng);
    ASSERT(rng->addr < rng->end);
    ASSERT(dl_contains(&ctx->head, &rng->dl));

    (void)ctx;

    dl_remove(&rng->dl);
}

static vmrange_t *vm_locate(context_t *ctx, size_t va) {
    ASSERT(NULL != ctx);
    ASSERT(ctx->spin.ticket_counter > ctx->spin.service_counter);
    ASSERT(INVALID_ADDR != va);

    for (dlnode_t *i = ctx->head.next; &ctx->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        if ((rng->addr <= va) && (va < rng->end)) {
            return rng;
        }
    }

    return NULL;
}


//------------------------------------------------------------------------------
// 分配虚拟地址空间并映射物理内存
//------------------------------------------------------------------------------

void context_init(context_t *ctx) {
    ctx->spin = SPIN_INIT;
    dl_init_circular(&ctx->head);
    ctx->table = mmu_create_table();
}

// 申请一段虚拟内存，分配物理内存，建立映射
void *context_alloc(context_t *ctx, int rank, page_type_t type, mmu_attr_t attrs, const char *desc) {
    ASSERT(NULL != ctx);

    size_t size = PAGE_SIZE << rank;

    int key = irq_spin_take(&ctx->spin);

    size_t va = vm_search(&g_kernel_ctx, DYNAMIC_MAP_ADDR, DYNAMIC_MAP_END, size);
    if (INVALID_ADDR == va) {
        irq_spin_give(&ctx->spin, key);
        return NULL;
    }

    size_t pa = pages_alloc(rank, type);
    if (INVALID_ADDR == pa) {
        irq_spin_give(&ctx->spin, key);
        return NULL;
    }

    vmrange_t *rng = kernel_heap_alloc(sizeof(vmrange_t));
    if (NULL == rng) {
        pages_free(pa);
        irq_spin_give(&ctx->spin, key);
        return NULL;
    }

    rng->addr = va;
    rng->end = va + size;
    rng->pa = pa;
    rng->attrs = attrs;
    rng->desc = desc;
    vm_insert(&g_kernel_ctx, rng);
    mmu_map(ctx->table, va, va + size, pa, attrs);

    irq_spin_give(&ctx->spin, key);
    return (void *)va;
}

// 释放虚拟内存，也释放映射的物理内存
void context_free(context_t *ctx, void *ptr) {
    ASSERT(NULL != ctx);
    ASSERT(NULL != ptr);
    ASSERT(DYNAMIC_MAP_ADDR <= (size_t)ptr);
    ASSERT((size_t)ptr < DYNAMIC_MAP_END);

    int key = irq_spin_take(&ctx->spin);

    vmrange_t *rng = vm_locate(&g_kernel_ctx, (size_t)ptr);
    ASSERT(NULL != rng);

    mmu_unmap(ctx->table, rng->addr, rng->end);
    pages_free(rng->pa);
    vm_remove(&g_kernel_ctx, rng);
    kernel_heap_free(rng->desc);
    kernel_heap_free(rng);

    irq_spin_give(&ctx->spin, key);
}

vmrange_t *context_query(context_t *ctx, size_t va) {
    ASSERT(NULL != ctx);
    ASSERT(INVALID_ADDR != va);

    int key = irq_spin_take(&ctx->spin);
    vmrange_t *rng = vm_locate(ctx, va);
    irq_spin_give(&ctx->spin, key);

    return rng;
}



//------------------------------------------------------------------------------
// 内核地址空间，也是所有进程共用的部分
//------------------------------------------------------------------------------


// TODO 有些 range 是全局变量，静态分配，这些 range 不能删除
//      还有一些是动态分配的，例如任务栈、内存池，这些 range 可以被删除


// 只初始化虚拟地址空间，暂不关心页表
INIT_TEXT void kernel_context_init() {
    g_kernel_ctx.spin = SPIN_INIT;
    dl_init_circular(&g_kernel_ctx.head);
    g_kernel_ctx.table = INVALID_ADDR;
}

context_t *get_kernel_context() {
    return &g_kernel_ctx;
}

// 将一段内核地址范围标记在地址空间中，返回这段空间
INIT_TEXT vmrange_t *kernel_context_mark(vmrange_t *rng, size_t va, size_t end,
        size_t pa, mmu_attr_t attrs, const char *desc) {
    ASSERT(NULL != rng);
    ASSERT(!dl_contains(&g_kernel_ctx.head, &rng->dl));
    ASSERT(va < end);

    // vmrange_t *rng = kernel_heap_alloc(sizeof(vmrange_t));
    // if (NULL == rng) {
    //     klog("fatal: cannot allocate vmrange!\n");
    //     return NULL;
    // }

    int key = irq_spin_take(&g_kernel_ctx.spin);
    rng->addr  = va;
    rng->end   = end;
    rng->pa    = pa;
    rng->attrs = attrs;
    rng->desc  = desc;
    vm_insert(&g_kernel_ctx, rng);
    irq_spin_give(&g_kernel_ctx.spin, key);

    return rng;
}

// 将内核地址空间里标记好的范围映射到物理地址空间
// 创建内核页表
INIT_TEXT void kernel_context_map_all() {
    int key = irq_spin_take(&g_kernel_ctx.spin);

    g_kernel_ctx.table = get_kernel_pgtable();

    for (dlnode_t *i = g_kernel_ctx.head.next; i != &g_kernel_ctx.head; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        size_t end = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        mmu_map(g_kernel_ctx.table, rng->addr, end, rng->pa, rng->attrs);
    }

    irq_spin_give(&g_kernel_ctx.spin, key);
}

// 该函数用于释放 .init 映射，因此函数不能为 INIT_TEXT
void kernel_context_unmap(vmrange_t *rng) {
    ASSERT(NULL != rng);

    int key = irq_spin_take(&g_kernel_ctx.spin);

    // 先移除 vmrange，最后清除页表中的映射
    // 因为 vmrange 可能就属于 init data

    vm_remove(&g_kernel_ctx, rng);

    size_t end = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    mmu_unmap(g_kernel_ctx.table, rng->addr, end);

    irq_spin_give(&g_kernel_ctx.spin, key);
}
