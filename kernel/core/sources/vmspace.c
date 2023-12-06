#include <vmspace.h>
#include <wheel.h>


void vmspace_init(vmspace_t *vm) {
    ASSERT(NULL != vm);
    dl_init_circular(&vm->head);
}

void vmspace_add(vmspace_t *vm, vmrange_t *rng) {
    ASSERT(NULL != vm);
    ASSERT(NULL != rng);
    ASSERT(!dl_contains(&vm->head, &rng->dl));

    // 在链表中寻找一个位置
    dlnode_t *node = vm->head.next;
    for (; &vm->head != node; node = node->next) {
        vmrange_t *cur = containerof(node, vmrange_t, dl);
        if (rng->addr < cur->addr) {
            ASSERT(rng->end <= cur->addr);
            break;
        }
    }

    // 将新的节点放入链表（此时 node 可能为头节点）
    dl_insert_before(&rng->dl, node);
}

void vmspace_show(vmspace_t *vm) {
    ASSERT(NULL != vm);

    klog("virtual mem space:\n");
    for (dlnode_t *i = vm->head.next; &vm->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        klog(" - 0x%zx..0x%zx %s\n", rng->addr, rng->end, rng->desc);
    }
}
