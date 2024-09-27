#include "vmspace.h"
#include "page.h"
#include <library/debug.h>
#include <arch_impl.h>


// 管理一套地址空间（有序链表）
// 通用管理结构，可以表示线性地址空间，也可以表示物理地址空间


static vmspace_t g_kernel_space;


INIT_TEXT void kernel_vmspace_init() {
    dl_init_circular(&g_kernel_space.head);
}

vmspace_t *kernel_vmspace() {
    return &g_kernel_space;
}

// INIT_TEXT void kernel_vmspace_mapall() {
//     kctx->table = mmu_create_kernel_table();
//     for (dlnode_t *i = kctx->space.head.next; i != &kctx->space.head; i = i->next) {
//         vmrange_t *rng = containerof(i, vmrange_t, dl);
//         size_t va_end = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
//         mmu_map(kctx->table, rng->addr, va_end, rng->pa, rng->attrs);
//     }
// }



// 创建新的地址空间，包括新的页表
void vmspace_create(vmspace_t *space) {
    ASSERT(NULL != space);
    dl_init_circular(&space->head);
    space->table = mmu_create_table();
}

vmrange_t *vmspace_locate(vmspace_t *space, size_t addr) {
    ASSERT(NULL != space);

    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        if ((rng->addr <= addr) && (addr < rng->end)) {
            return rng;
        }
    }

    return NULL;
}

// 在地址空间中添加一个范围
void vmspace_insert(vmspace_t *space, vmrange_t *rng) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);
    ASSERT(rng->addr < rng->end);
    ASSERT(0 == (rng->addr & (PAGE_SIZE - 1)));
    ASSERT(0 == (rng->pa & (PAGE_SIZE - 1)));

    dlnode_t *node = space->head.next;
    for (; &space->head != node; node = node->next) {
        vmrange_t *cur = containerof(node, vmrange_t, dl);
        if (rng->addr < cur->addr) {
            ASSERT(rng->end <= cur->addr);
            break;
        }
        ASSERT(cur->addr < cur->end);
        ASSERT(cur->end <= rng->addr);
    }

    dl_insert_before(&rng->dl, node);
}

void vmspace_remove(vmspace_t *space, vmrange_t *rng) {
    // ASSERT(NULL != space);
    ASSERT(NULL != rng);

    if (NULL == space) {
        space = &g_kernel_space;
    }
    ASSERT(dl_contains(&space->head, &rng->dl));

    if (space->table) {
        size_t end = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        mmu_unmap(space->table, rng->addr, end);
    }

    if (0 != rng->pa) {
        // TODO init section 映射的物理内存不是一个 block
        page_block_free(rng->pa);
    }

    dl_remove(&rng->dl);
}

void vmspace_show(vmspace_t *space) {
    ASSERT(NULL != space);

    log("vmspace:\n");
    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        log("  - vm 0x%016zx~0x%016zx %s\n", rng->addr, rng->end, rng->desc);
    }
}


//------------------------------------------------------------------------------
// 分配整块内存并映射
//------------------------------------------------------------------------------

// 在地址空间中寻找一段范围，页对齐，前后留出 guard page
// 并且分配物理内存，在页表中添加映射
size_t vmspace_alloc(vmspace_t *space, vmrange_t *rng, size_t start, size_t end,
        int rank, page_type_t type, mmu_attr_t attrs) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);
    ASSERT(!dl_contains(&space->head, &rng->dl));
    ASSERT(0 != start);
    ASSERT(start < end);

    size_t size = PAGE_SIZE << rank;
    rng->addr = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    rng->end = rng->addr + size;
    rng->pa = 0;    // 物理地址未分配

    // 从前到后顺序遍历，遇到第一个满足大小要求的空间就跳出
    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *ref = containerof(i, vmrange_t, dl);
        size_t rng_start = ref->addr & ~(PAGE_SIZE - 1);
        size_t rng_end = (ref->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        rng_start -= PAGE_SIZE; // 开头留出 guard page
        rng_end += PAGE_SIZE;   // 结尾留出 guard page

        if (rng->end <= rng_start) {
            break;
        }

        rng->addr = rng_end;
        rng->end = rng_end + size;
    }

    if (rng->end > end) {
        return 0;
    }

    rng->pa = page_block_alloc(rank, type);
    if (0 == rng->pa) {
        // TODO 如果连续内存分配失败，尝试分配不连续的物理页
        return 0;
    }

    mmu_map(space->table, rng->addr, rng->end, rng->pa, attrs);
    vmspace_insert(space, rng);
    return rng->addr;
}

size_t alloc_kernel_stack(vmrange_t *rng, int rank) {
    return vmspace_alloc(&g_kernel_space, rng,
        KERNEL_STACK_ZONE_ADDR, KERNEL_STACK_ZONE_END,
        rank, PT_KERNEL_STACK, MMU_WRITE);
}
