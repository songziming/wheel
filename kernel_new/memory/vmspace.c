#include "vmspace.h"
#include <library/debug.h>
#include <arch_impl.h>


// 管理一套地址空间（有序链表）
// 通用管理结构，可以表示线性地址空间，也可以表示物理地址空间


void vmspace_init(vmspace_t *space) {
    ASSERT(NULL != space);
    dl_init_circular(&space->head);
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

vmrange_t *vm_locate(vmspace_t *space, size_t addr) {
    ASSERT(NULL != space);

    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        if ((rng->addr <= addr) && (addr < rng->end)) {
            return rng;
        }
    }

    return NULL;
}

void vmspace_show(vmspace_t *space) {
    ASSERT(NULL != space);

    log("vmspace:\n");
    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        log("  - vm 0x%016zx~0x%016zx %s\n", rng->addr, rng->end, rng->desc);
    }
}
