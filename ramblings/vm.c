#include <wheel.h>
#include <arch_api.h>
#include <dllist.h>
#include <debug.h>


// 管理虚拟地址空间，纯数据结构，没有锁

typedef struct vmrange {
    dlnode_t    dl;
    size_t      vaddr;  // 可能未对齐到页
    size_t      vend;   // 可能未对齐到页
} vmrange_t;

typedef dlnode_t vmspace_t;


void vmspace_init(vmspace_t *space) {
    dl_init_circular(space);
}

// 寻找一段空闲的虚拟地址，大小为 size，范围是 [from,to)
// 分配的 range 起始地址对齐到页
// 和已有的 vmrange 之间留出 guard page
// 成功找到地址返回 1，未找到则返回 0
int vmspace_alloc(vmspace_t *space, vmrange_t *rng, size_t size, size_t from, size_t to) {
    ASSERT(!dl_contains(space, &rng->dl));

    rng->vaddr = from;
    rng->vend = rng->vaddr + size;

    // 从前到后顺序遍历，遇到第一个满足大小要求的空间就跳出
    for (dlnode_t *i = space->next; space != i; i = i->next) {
        vmrange_t *ref = containerof(i, vmrange_t, dl);
        size_t rng_start = ref->vaddr & ~(PAGE_SIZE - 1);
        size_t rng_end = (ref->vend + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        rng_start -= PAGE_SIZE; // 开头留出 guard page
        rng_end += PAGE_SIZE;   // 结尾留出 guard page

        if (rng->vend <= rng_start) {
            dl_insert_before(&rng->dl, i);
            return 1;
        }

        rng->vaddr = rng_end;
        rng->vend = rng_end + size;
    }

    if (rng->vend <= to) {
        dl_insert_before(&rng->dl, space);
        return 1;
    }

    return 0;
}

// 分配指定的地址，检查和现有 range 是否冲突
// 不能和已有 range 重叠，但是可以没有 guard-page
// 没有冲突，分配成功则返回 1，有冲突则返回 0
int vmspace_alloc_at(vmspace_t *space, vmrange_t *rng, size_t va, size_t size) {
    ASSERT(!dl_contains(space, &rng->dl));

    rng->vaddr = va;
    rng->vend = va + size;

    for (dlnode_t *i = space->next; space != i; i = i->next) {
        vmrange_t *ref = containerof(i, vmrange_t, dl);
        if (rng->vend <= ref->vaddr) { // 完整地位于 ref 之前，无冲突
            dl_insert_before(&rng->dl, i);
            return 1;
        }
        if (rng->vaddr < ref->vend) { // 有冲突
            return 0;
        }
    }

    dl_insert_before(&rng->dl, space);
    return 1;
}

// 寻找虚拟地址 va 所在的 range
vmrange_t *vmspace_find(vmspace_t *space, size_t va) {
    for (dlnode_t *i = space->next; space != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        if ((rng->vaddr <= va) && (va < rng->vend)) {
            return rng;
        }
    }
    return NULL;
}

void vmspace_remove(vmspace_t *space, vmrange_t *rng) {
    ASSERT(dl_contains(space, &rng->dl));
    (void)space;
    dl_remove(&rng->dl);
}

