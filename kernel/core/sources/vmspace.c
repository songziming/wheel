#include <vmspace.h>
#include <wheel.h>



// vmspace_t g_kernel_proc.space;


void vmspace_init(vmspace_t *vm) {
    ASSERT(NULL != vm);
    dl_init_circular(&vm->head);
}

void vmrange_init(vmrange_t *rng, size_t addr, size_t end, const char *desc) {
    ASSERT(NULL != rng);

    rng->addr = addr;
    rng->end  = end;
    rng->desc = desc;
}

void vmspace_insert(vmspace_t *vm, vmrange_t *rng) {
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

void vmspace_remove(vmspace_t *vm, vmrange_t *rng) {
    ASSERT(NULL != vm);
    ASSERT(NULL != rng);
    ASSERT(dl_contains(&vm->head, &rng->dl));

    (void)vm;

    dl_remove(&rng->dl);
}

// 返回目标虚拟地址所在的 range
// 该函数可能用在中断处理函数中
vmrange_t *vmspace_locate(vmspace_t *vm, size_t va) {
    ASSERT(NULL != vm);

    for (dlnode_t *i = vm->head.next; &vm->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        if ((rng->addr <= va) && (va < rng->end)) {
            return rng;
        }
    }

    return NULL;
}

// 寻找一段未分配的虚拟地址范围，首尾保留 guard page
size_t vmspace_search(vmspace_t *vm, size_t start, size_t end, size_t size) {
    ASSERT(NULL != vm);

    // 如果 vmspace 为空，则无需关心 guard pages
    if (dl_is_lastone(&vm->head) && (start + size <= end)) {
        return start;
    }

    size_t prev_end = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (dlnode_t *i = vm->head.next; &vm->head!= i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        size_t rng_start = rng->addr & ~(PAGE_SIZE - 1);
        size_t rng_end = (rng->end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        if (1) { // HEAD_GUARD_PAGE
            rng_start -= PAGE_SIZE;
        }
        if (1) { // TAIL_GUARD_PAGE
            rng_end += PAGE_SIZE;
        }

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


void vmspace_show(vmspace_t *vm) {
    ASSERT(NULL != vm);

    klog("virtual mem space:\n");
    for (dlnode_t *i = vm->head.next; &vm->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        klog("  - 0x%zx..0x%zx %s\n", rng->addr, rng->end, rng->desc);
    }
}
