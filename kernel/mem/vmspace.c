#include "vmspace.h"
#include <arch_api.h>
#include <debug.h>


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

void vmspace_show(vmspace_t *space) {
    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        logk("  - vm 0x%016zx~0x%016zx %s\n", rng->vaddr, rng->vend, rng->desc);
    }
}
