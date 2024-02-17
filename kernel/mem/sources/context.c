// 管理一套地址空间，包括 vm 链表和分页表

#include <context.h>
#include <wheel.h>
#include <spin.h>


static context_t g_kernel_ctx;

void context_init(context_t *ctx) {
    ctx->lock = SPIN_INIT;
    vmspace_init(&ctx->space);
    ctx->table = mmu_create_table();
}

void context_map(context_t *ctx, size_t va, size_t size, size_t pa, mmu_attr_t attrs, const char *desc) {
    ASSERT(NULL != ctx);
    ASSERT(INVALID_ADDR != va);
    ASSERT(0 == (va   & (PAGE_SIZE - 1)));
    ASSERT(0 == (size & (PAGE_SIZE - 1)));
    // ASSERT(INVALID_ADDR != pa);
    // ASSERT(0 == (pa   & (PAGE_SIZE - 1)));

    int key = irq_spin_take(&ctx->lock);
    vmrange_t *rng = kernel_heap_alloc(sizeof(vmrange_t));
    vmspace_insert(&ctx->space, rng, va, va + size, desc);
    if (INVALID_ADDR != pa) {
        ASSERT(0 == (pa & (PAGE_SIZE - 1)));
        mmu_map(ctx->table, va, va + size, pa, attrs);
    }
    irq_spin_give(&ctx->lock, key);
}

void context_unmap(context_t *ctx, size_t va, size_t size) {
    ASSERT(NULL != ctx);
    ASSERT(INVALID_ADDR != va);
    ASSERT(0 == (va   & (PAGE_SIZE - 1)));
    ASSERT(0 == (size & (PAGE_SIZE - 1)));

    int key = irq_spin_take(&ctx->lock);
    vmrange_t *rng = vmspace_locate(&ctx->space, va);
    vmspace_remove(&ctx->space, rng);
    kernel_heap_free(rng);
    mmu_unmap(ctx->table, va, va + size);
    irq_spin_give(&ctx->lock, key);
}

// 申请一段虚拟内存，分配物理内存，建立映射
void *context_alloc(context_t *ctx, int rank, page_type_t type, mmu_attr_t attrs, const char *desc) {
    ASSERT(NULL != ctx);

    size_t size = PAGE_SIZE << rank;

    int key = irq_spin_take(&ctx->lock);

    size_t va = vmspace_search(&ctx->space, DYNAMIC_MAP_ADDR, DYNAMIC_MAP_END, size);
    if (INVALID_ADDR != va) {
        irq_spin_give(&ctx->lock, key);
        return NULL;
    }

    size_t pa = pages_alloc(rank, type);
    if (INVALID_ADDR != pa) {
        irq_spin_give(&ctx->lock, key);
        return NULL;
    }

    vmrange_t *rng = kernel_heap_alloc(sizeof(vmrange_t));
    if (NULL == rng) {
        pages_free(pa);
        irq_spin_give(&ctx->lock, key);
        return NULL;
    }

    vmspace_insert(&ctx->space, rng, va, va + size, desc);
    mmu_map(ctx->table, va, va + size, pa, attrs);

    irq_spin_give(&ctx->lock, key);
    return (void *)va;
}

INIT_TEXT void kernel_context_init() {
    g_kernel_ctx.lock = SPIN_INIT;
    vmspace_init(&g_kernel_ctx.space);
    g_kernel_ctx.table = get_kernel_pgtable();
}

context_t *get_kernel_context() {
    return &g_kernel_ctx;
}

// 将一段内核地址范围标记在地址空间中，返回这段空间
INIT_TEXT vmrange_t *kernel_context_mark(size_t va, size_t end, const char *desc) {
    ASSERT(va < end);

    vmrange_t *rng = kernel_heap_alloc(sizeof(vmrange_t));
    if (NULL == rng) {
        klog("fatal: cannot allocate vmrange!\n");
        return NULL;
    }

    int key = irq_spin_take(&g_kernel_ctx.lock);
    vmspace_insert(&g_kernel_ctx.space, rng, va, end, desc);
    irq_spin_give(&g_kernel_ctx.lock, key);

    return rng;
}

// 将内核地址空间里标记好的范围映射到物理地址空间
INIT_TEXT void kernel_context_mapall() {
    int key = irq_spin_take(&g_kernel_ctx.lock);

    for (dlnode_t *i = g_kernel_ctx.space.head.next; i != &g_kernel_ctx.space.head; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        size_t end = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        mmu_map(g_kernel_ctx.table, rng->addr, end, rng->addr & ~KERNEL_TEXT_ADDR, MMU_WRITE);
    }

    irq_spin_give(&g_kernel_ctx.lock, key);
}

INIT_TEXT void kernel_context_unmap(vmrange_t *rng) {
    int key = irq_spin_take(&g_kernel_ctx.lock);

    size_t end = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    mmu_unmap(g_kernel_ctx.table, rng->addr, end);

    irq_spin_give(&g_kernel_ctx.lock, key);
}
