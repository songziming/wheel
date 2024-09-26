#include "context.h"
#include <arch_intf.h>
#include <arch_impl.h>
#include <library/debug.h>
#include <memory/page.h>


// 管理进程的地址空间，以及内核的地址空间
// 进程地址空间被所有进程共用


// arch 负责初始化
static context_t g_kernel_ctx;

context_t *kernel_context() {
    return &g_kernel_ctx;
}

void context_init(context_t *ctx) {
    vmspace_init(&ctx->space);
    ctx->table = mmu_create_table();
}

// 分配物理地址，并映射到地址空间中，返回分配的虚拟地址
void *context_alloc(context_t *ctx, vmrange_t *rng, int rank) {
    // ASSERT(NULL != ctx);
    ASSERT(NULL != rng);

    if (NULL == ctx) {
        ctx = &g_kernel_ctx;
    }

    size_t size = PAGE_SIZE << rank;
    size_t va = vmspace_alloc(&ctx->space, rng, DIRECT_MAP_ADDR, KERNEL_TEXT_ADDR, size);
    if (0 == va) {
        return NULL;
    }

    rng->pa = page_block_alloc(rank, PT_KERNEL);
    if (0 == rng->pa) {
        vmspace_remove(&ctx->space, rng);
        return NULL;
    }

    mmu_map(ctx->table, va, va + size, rng->pa, MMU_WRITE);
    return (void *)va;
}
