#include "vmspace.h"
#include <arch_api.h>
#include "page.h"
#include <debug.h>


// 内核地址空间布局
vmspace_t g_kernel_vm;


// 创建新的地址空间，包括内核部分的映射
void vmspace_init(vmspace_t *space) {
    ASSERT(NULL != space);
    dl_init_circular(&space->head);
}

vmrange_t *vmspace_find(vmspace_t *space, size_t addr) {
    ASSERT(NULL != space);

    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        if ((rng->vaddr <= addr) && (addr < rng->vend)) {
            return rng;
        }
    }

    return NULL;
}

// 在地址空间中添加一个范围，不操作物理地址
void vmspace_insert(vmspace_t *space, vmrange_t *rng) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);
    ASSERT(rng->vaddr < rng->vend);
    ASSERT(0 == (rng->vaddr & (PAGE_SIZE - 1)));

    dlnode_t *node = space->head.next;
    for (; &space->head != node; node = node->next) {
        vmrange_t *cur = containerof(node, vmrange_t, dl);
        if (rng->vaddr < cur->vaddr) {
            ASSERT(rng->vend <= cur->vaddr);
            break;
        }
        ASSERT(cur->vaddr < cur->vend);
        ASSERT(cur->vend <= rng->vaddr);
    }

    dl_insert_before(&rng->dl, node);
}

// 在地址空间中寻找一段范围，页对齐，前后留出 guard page
// 并且分配物理内存，在页表中添加映射
size_t vmspace_alloc(vmspace_t *space, vmrange_t *rng, size_t start, size_t end,
        uint32_t rank, uint32_t type, mmu_attr_t attrs) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);
    ASSERT(!dl_contains(&space->head, &rng->dl));
    ASSERT(0 != start);
    ASSERT(start < end);

    size_t size = PAGE_SIZE << rank;
    rng->vaddr = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    rng->vend = rng->vaddr + size;
    rng->attrs = attrs;
    rng->paddr = 0; // 物理地址未分配

    // 从前到后顺序遍历，遇到第一个满足大小要求的空间就跳出
    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *ref = containerof(i, vmrange_t, dl);
        size_t rng_start = ref->vaddr & ~(PAGE_SIZE - 1);
        size_t rng_end = (ref->vend + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        rng_start -= PAGE_SIZE; // 开头留出 guard page
        rng_end += PAGE_SIZE;   // 结尾留出 guard page

        if (rng->vend <= rng_start) {
            break;
        }

        rng->vaddr = rng_end;
        rng->vend = rng_end + size;
    }

    if (rng->vend > end) {
        return 0;
    }

    rng->paddr = page_alloc(rank, type);
    if (0 == rng->paddr) {
        // TODO 如果连续内存分配失败，尝试分配不连续的物理页
        //      拆分成更小的块，组成 pglist
        return 0;
    }

    mmu_map(space->table, rng->vaddr, rng->vend, rng->paddr, rng->attrs);
    vmspace_insert(space, rng);
    return rng->vaddr;
}

size_t vmspace_alloc_stack(vmspace_t *space, vmrange_t *rng, uint32_t rank) {
    return vmspace_alloc(space, rng,
        STACK_ZONE_START, STACK_ZONE_END,
        rank, PT_STACK, MMU_WRITE);
}

void vmspace_remove(vmspace_t *space, vmrange_t *rng) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);
    ASSERT(dl_contains(&space->head, &rng->dl));

    if (space->table) {
        mmu_unmap(space->table, rng->vaddr, rng->vend);
    }

    if (0 != rng->paddr) {
        // TODO init section 映射的物理内存不是一个 block
        page_free(rng->paddr);
    }

    dl_remove(&rng->dl);
}

void vmspace_show(vmspace_t *space) {
    logk("virtual address space:\n");
    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        logk("  - vm %016zx~%016zx pa %016zx %s\n", rng->vaddr, rng->vend, rng->paddr, rng->desc);
    }
}
