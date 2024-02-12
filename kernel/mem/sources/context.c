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

INIT_TEXT void kernel_context_init() {
    g_kernel_ctx.lock = SPIN_INIT;
    vmspace_init(&g_kernel_ctx.space);
    g_kernel_ctx.table = get_kernel_pgtable();
}

context_t *get_kernel_context() {
    return &g_kernel_ctx;
}
