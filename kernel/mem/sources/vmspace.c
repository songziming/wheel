#include <vmspace.h>
#include <debug.h>

void vmspace_init(vmspace_t *vm) {
    ASSERT(NULL != vm);
    dl_init_circular(&vm->head);
}


#ifdef DEBUG

// 判断节点是否位于链表中
static int inside(vmspace_t *vm, vmrange_t *rng) {
    for (dlnode_t *i = vm->head.next; &vm->head != i; i = i->next) {
        if (&rng->dl == i) {
            return 1;
        }
    }
    return 0;
}

#endif // DEBUG


void vmspace_insert_range(vmspace_t *vm, vmrange_t *rng) {
    ASSERT(NULL != vm);
    ASSERT(NULL != rng);
    ASSERT(0 != rng->size);
    ASSERT(!inside(vm, rng));

    // 在链表中寻找一个位置
    dlnode_t *node = vm->head.next;
    for (; &vm->head != node; node = node->next) {
        vmrange_t *cur = containerof(node, vmrange_t, dl);
        if (rng->addr < cur->addr) {
            ASSERT(rng->addr + rng->size <= cur->addr);
            break;
        }
    }

    // 将新的节点放入链表（此时 node 可能为头节点）
    dl_insert_before(&rng->dl, node);
}

void vmspace_remove_range(vmspace_t *vm, vmrange_t *rng) {
    ASSERT(NULL != vm);
    ASSERT(NULL != rng);
    ASSERT(inside(vm, rng));

    (void)vm;

    dl_remove(&rng->dl);
}

#ifdef DEBUG

void vmspace_show(vmspace_t *vm) {
    ASSERT(NULL != vm);

    size_t last = 0;
    size_t max_size = 0;
    size_t max_gap = 0;
    for (dlnode_t *i = vm->head.next; &vm->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);

        if (max_size < rng->size) {
            max_size = rng->size;
        }
        if ((0 != last) && (max_gap < rng->addr - last)) {
            max_gap = rng->addr - last;
        }

        last = rng->addr + rng->size;
    }

    int size_digits = 1;
    int gap_digits = 1;
    while (max_size >= 16) {
        max_size /= 16;
        ++size_digits;
    }
    while (max_gap >= 16) {
        max_gap /= 16;
        ++gap_digits;
    }

    last = 0;
    for (dlnode_t *i = vm->head.next; &vm->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);

        size_t end = rng->addr + rng->size;
        size_t gap = 0;
        if (0 != last) {
            gap = rng->addr - last;
        }
        last = end;

        dbg_print("range:0x%016lx~0x%016lx, size:%#*lx, gap:%#*lx, %s\n",
                rng->addr, end, size_digits + 2, rng->size, gap_digits + 2, gap, rng->desc);
    }
}

#endif // DEBUG
