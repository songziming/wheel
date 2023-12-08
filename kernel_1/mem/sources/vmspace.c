// 记录地址空间布局，支持 demand paging

#include <vmspace.h>
#include <debug.h>
#include <page.h>


// 将 range 添加到地址空间中，不涉及自旋锁
static void _insert_range(vmspace_t *vm, vmrange_t *rng) {
    ASSERT(NULL != vm);
    ASSERT(NULL != rng);
    ASSERT(0 != rng->size);

#ifdef DEBUG
    // 确认新节点不在链表中
    int range_in_space = 0;
    for (dlnode_t *i = vm->dl_head.next; &vm->dl_head != i; i = i->next) {
        if (&rng->dl == i) {
            range_in_space = 1;
        }
    }
    ASSERT(!range_in_space);
#endif

    // 在链表中寻找一个位置
    dlnode_t *node = vm->dl_head.next;
    for (; &vm->dl_head != node; node = node->next) {
        vmrange_t *cur = containerof(node, vmrange_t, dl);
        if (rng->va < cur->va) {
            ASSERT(rng->va + rng->size <= cur->va);
            break;
        }
    }

    // 将新的节点放入链表（此时 node 可能为头节点）
    dl_insert_before(&rng->dl, node);
}


// 创建一个空的地址空间，没有任何内容
void vmspace_init(vmspace_t *vm) {
    ASSERT(NULL != vm);
    dl_init_circular(&vm->dl_head);
    vm->lock = SPIN_INIT;
}

void vmspace_insert_range(vmspace_t *vm, vmrange_t *rng) {
    int key = irq_spin_take(&vm->lock);
    _insert_range(vm, rng);
    irq_spin_give(&vm->lock, key);
}

// 释放一段虚拟地址空间
void vmspace_remove_range(vmspace_t *vm, vmrange_t *rng) {
    ASSERT(NULL != vm);
    ASSERT(NULL != rng);

    int key = irq_spin_take(&vm->lock);

#ifdef DEBUG
    // 确认节点位于链表中
    int range_in_space = 0;
    for (dlnode_t *i = vm->dl_head.next; &vm->dl_head != i; i = i->next) {
        if (&rng->dl == i) {
            range_in_space = 1;
        }
    }
    ASSERT(range_in_space);
#endif

    dl_remove(&rng->dl);
    irq_spin_give(&vm->lock, key);
}

// 返回目标虚拟地址所在的 range
// 该函数可能用在中断处理函数中
vmrange_t *vmspace_locate_range(vmspace_t *vm, size_t va) {
    ASSERT(NULL != vm);

    int key = irq_spin_take(&vm->lock);

    for (dlnode_t *i = vm->dl_head.next; &vm->dl_head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        size_t start = rng->va;
        size_t end = start + rng->size;

        if (RF_GUARD & rng->flags) {
            start -= PAGE_SIZE;
            end += PAGE_SIZE;
        }

        if ((start <= va) && (va < end)) {
            irq_spin_give(&vm->lock, key);
            return rng;
        }
    }

    irq_spin_give(&vm->lock, key);
    return NULL;
}



// 寻找一段未使用的 range，必须在限制的范围内，而且是页对齐的
// 找到第一段可用的就返回，返回可用的虚拟地址，如果找不到符合要求的地址就返回 -1
static size_t _find_unused(vmspace_t *vm, size_t size, size_t start, size_t end, range_flag_t flags) {
    ASSERT(NULL != vm);

    // 如果 vmspace 为空，无需关心 guard pages
    if (dl_is_lastone(&vm->dl_head) && (start + size <= end)) {
        return start;
    }

    size_t prev_end = start;

    for (dlnode_t *i = vm->dl_head.next; &vm->dl_head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        size_t rng_start = rng->va & ~(PAGE_SIZE - 1);
        size_t rng_end = (rng->va + rng->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        if ((RF_GUARD & flags) || (RF_GUARD & rng->flags)) {
            rng_start -= PAGE_SIZE;
            rng_end += PAGE_SIZE;
        }

        if (rng_end < prev_end) {
            continue;
        }
        if (rng_start > end) {
            rng_start = end;
        }

        if (prev_end + size <= rng_start) {
            return prev_end;
        }
        prev_end = rng_end;
        if (prev_end >= end) {
            break;
        }
    }

    // 还需要检查最后一个range
    if (prev_end + size <= end) {
        return prev_end;
    }

    return INVALID_ADDR;
}

#if 0
size_t vmspace_find_unused(vmspace_t *vm, size_t size) {
    return _find_unused(vm, size, vm->start, vm->end, 1);
}

// 寻找一段未使用的地址范围，且保证这段范围前后留有 guard page
size_t vmspace_find_unused_guarded(vmspace_t *vm, size_t size) {
    size += PAGE_SIZE * 2;

    size_t start = vm->start - PAGE_SIZE;
    size_t end = vm->end + PAGE_SIZE;

    // 寻找的目标范围已经包含了 guard，因此就不用检查已有 range 的 guard
    size_t va = _find_unused(vm, size, start, end, 0);
    if (INVALID_ADDR != va) {
        va += PAGE_SIZE;
    }

    return va;
}

// 分配一段虚拟地址范围，将分配到的范围记录在 rng，并将 rng 注册到地址空间
// 分配成功则返回 0，分配失败，则返回 1
int vmspace_alloc(vmspace_t *vm, size_t size, vmrange_t *rng) {
    ASSERT(0 == (size & (PAGE_SIZE - 1)));

    // rng->va = vmspace_find_unused(vm, size);
    rng->va = _find_unused(vm, size, vm->start, vm->end, WITH_GUARD);
    if (INVALID_ADDR == rng->va) {
        return 1;
    }

    rng->size = size;
    rng->flags = 0;
    vmspace_insert_range(vm, rng);
    return 0;
}

// 分配一段虚拟地址范围，并保证前后留有 guard page
int vmspace_alloc_guarded(vmspace_t *vm, size_t size, vmrange_t *rng) {
    ASSERT(0 == (size & (PAGE_SIZE - 1)));

    // rng->va = vmspace_find_unused_guarded(vm, size);
    size_t req_size = size + PAGE_SIZE * 2;
    size_t start = vm->start - PAGE_SIZE;
    size_t end = vm->end + PAGE_SIZE;
    rng->va = _find_unused(vm, req_size, start, end, NO_GUARD);
    if (INVALID_ADDR == rng->va) {
        return 1;
    }

    rng->size = size;
    rng->flags = RF_GUARD;
    vmspace_insert_range(vm, rng);
    return 0;
}
#endif


// 寻找一段满足要求的虚拟地址范围，返回虚拟地址
size_t vmspace_reserve(vmspace_t *vm, size_t size, vmrange_t *rng, size_t start, size_t end, range_flag_t flags) {
    int key = irq_spin_take(&vm->lock);

    size_t va = _find_unused(vm, size, start, end, flags);
    if (INVALID_ADDR == va) {
        irq_spin_give(&vm->lock, key);
        return INVALID_ADDR;
    }

    rng->va = va;
    rng->size = size;
    rng->flags = flags;
    _insert_range(vm, rng);

    irq_spin_give(&vm->lock, key);
    return va;
}




void vmspace_show(vmspace_t *vm) {
    ASSERT(NULL != vm);

    size_t last = 0;
    size_t max_size = 0;
    size_t max_gap = 0;
    for (dlnode_t *i = vm->dl_head.next; &vm->dl_head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);

        if (max_size < rng->size) {
            max_size = rng->size;
        }
        if ((0 != last) && (max_gap < rng->va - last)) {
            max_gap = rng->va - last;
        }

        last = rng->va + rng->size;
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
    for (dlnode_t *i = vm->dl_head.next; &vm->dl_head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);

        size_t end = rng->va + rng->size;
        size_t gap = 0;
        if (0 != last) {
            gap = rng->va - last;
        }
        last = end;

        dbg_print("range:0x%016lx~0x%016lx, size:%#*lx, gap:%#*lx, %s\n",
                rng->va, end, size_digits + 2, rng->size, gap_digits + 2, gap, rng->desc);
    }
}
