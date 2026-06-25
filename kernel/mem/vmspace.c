#include "vmspace.h"
#include <arch_api.h>
#include "page.h"
#include <debug.h>
#include <kshell.h>
#include <console.h>


// 内核地址空间布局
vmspace_t g_kernel_vm;


// 在地址空间中添加一个范围，不操作物理地址
// 不要求前后保留 guard-page
// 没有冲突则返回 1，有冲突返回 0
static int vm_alloc_at(vmspace_t *space, vmrange_t *rng) {
    ASSERT(!dl_contains(&space->head, &rng->dl));

    dlnode_t *node = space->head.next;
    for (; &space->head != node; node = node->next) {
        vmrange_t *cur = containerof(node, vmrange_t, dl);
        if (rng->vaddr < cur->vaddr) {
            ASSERT(rng->vend <= cur->vaddr);
            break;
        }
        if (rng->vaddr < cur->vend) {
            return 0; // 有冲突
        }
    }

    dl_insert_before(&rng->dl, node);
    return 1;
}

// 寻找一段虚拟内存范围，记录在 rng 里面
// 找到了返回 1，否则返回 0
static int vm_alloc(vmspace_t *space, vmrange_t *rng, size_t size) {
    rng->vaddr = space->dyn_start;
    rng->vend = rng->vaddr + size;
    rng->attrs = MMU_NONE;

    // 从前到后顺序遍历，遇到第一个满足大小要求的空间就跳出
    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
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

    // rng 添加到最后
    if (rng->vend <= space->dyn_end) {
        dl_insert_before(&rng->dl, &space->head);
        return 1;
    }

    return 0;
}





// 创建新的地址空间，包括内核部分的映射
void vmspace_init(vmspace_t *space, size_t start, size_t end) {
    ASSERT(NULL != space);
    dl_init_circular(&space->head);
    space->lock = SPINLOCK_INIT;
    space->dyn_start = start + PAGE_SIZE - 1;
    space->dyn_start &= ~(PAGE_SIZE - 1);
    space->dyn_end = end & ~(PAGE_SIZE - 1);
}

vmrange_t *vmspace_lookup(vmspace_t *space, size_t addr) {
    ASSERT(NULL != space);
    SPINLOCK_SCOPED(&space->lock);
    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        if ((rng->vaddr <= addr) && (addr < rng->vend)) {
            return rng;
        }
    }
    return NULL;
}

// 在地址空间中添加一个范围，不操作物理地址
INIT_TEXT void vmspace_insert(vmspace_t *space, vmrange_t *rng) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);
    ASSERT(rng->vaddr < rng->vend);
    ASSERT(0 == (rng->vaddr & (PAGE_SIZE - 1)));

    SPINLOCK_SCOPED(&space->lock);
    vm_alloc_at(space, rng);
}

// 划分一段虚拟内存，不映射物理内存
void *vmspace_alloc_nomap(vmspace_t *space, vmrange_t *rng, size_t size) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);

    SPINLOCK_SCOPED(&space->lock);
    ASSERT(!dl_contains(&space->head, &rng->dl));

    if (0 == vm_alloc(space, rng, size)) {
        // 找不到合适的虚拟地址范围，直接退出
        return NULL;
    }

    rng->pages.head = 0;
    rng->pages.tail = 0;
    return (void*)rng->vaddr;
}

void *vmspace_alloc(vmspace_t *space, vmrange_t *rng, size_t size,
        page_type_t type, mmu_attr_t attrs) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);

    size += PAGE_SIZE - 1;
    size &= ~(PAGE_SIZE - 1);

    SPINLOCK_SCOPED(&space->lock);
    ASSERT(!dl_contains(&space->head, &rng->dl));

    if (0 == vm_alloc(space, rng, size)) {
        logk("cannot reserve vmrange of size-0x%zx\n", size);
        return NULL;
    }

    rng->pages.head = 0;
    rng->pages.tail = 0;
    pagelist_alloc(&rng->pages, size >> PAGE_SHIFT, type);
    // TODO 检查pagelist申请是否成功，如果失败需要删除 rng

    size_t va = rng->vaddr;
    for (uint32_t blk = rng->pages.head; blk; blk = g_pages[blk].next) {
        size_t blksize = PAGE_SIZE << g_pages[blk].rank;
        mmu_map(space->table, va, va + blksize, (size_t)blk << PAGE_SHIFT, attrs);
        va += blksize;
    }

    rng->attrs = attrs;
    return (void*)rng->vaddr;
}

// TODO 没有检查地址范围是否有冲突
void *vmspace_alloc_at(vmspace_t *space, vmrange_t *rng,
        size_t addr, size_t size, page_type_t type, mmu_attr_t attrs) {
    rng->vaddr = addr;
    rng->vend = addr + size;
    rng->attrs = attrs;

    SPINLOCK_SCOPED(&space->lock);
    if (0 == vm_alloc_at(space, rng)) {
        logk("range %zx:%zx conflict with existing\n", addr, size);
        return NULL;
    }

    rng->pages.head = 0U;
    rng->pages.tail = 0U;
    size += PAGE_SIZE - 1;
    pagelist_alloc(&rng->pages, size >> PAGE_SHIFT, type);
    // TODO 检查pagelist申请是否成功

    size_t va = rng->vaddr;
    for (uint32_t blk = rng->pages.head; blk; blk = g_pages[blk].next) {
        size_t blksize = PAGE_SIZE << g_pages[blk].rank;
        mmu_map(space->table, va, va + blksize, (size_t)blk << PAGE_SHIFT, attrs);
        va += blksize;
    }
}

void *vmspace_alloc_kstack(vmspace_t *space, vmrange_t *rng) {
    return vmspace_alloc(space, rng, KSTACK_SIZE, PT_STACK, MMU_WRITE);
}
void *vmspace_alloc_ustack(vmspace_t *space, vmrange_t *rng) {
    return vmspace_alloc(space, rng, USTACK_SIZE, PT_STACK, MMU_WRITE|MMU_USER);
}

// 映射地址不变，仅改变属性
// 加载用户态代码段、数据段之后，调用此函数，去掉读权限
// 此时只有一个线程使用此页表，不用执行 tlb-shootdown
void vmspace_remap(vmspace_t *space, vmrange_t *rng, mmu_attr_t attrs) {
    SPINLOCK_SCOPED(&space->lock);
    ASSERT(dl_contains(&space->head, &rng->dl));

    rng->attrs = attrs;

    // 物理地址可能是不连续的，需要遍历 page-list
    size_t va = rng->vaddr;
    for (uint32_t blk = rng->pages.head; blk; blk = g_pages[blk].next) {
        size_t blksize = PAGE_SIZE << g_pages[blk].rank;
        mmu_map(space->table, va, va + blksize, (size_t)blk << PAGE_SHIFT, attrs);
        va += blksize;
    }
}

void vmspace_remove(vmspace_t *space, vmrange_t *rng) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);

    SPINLOCK_SCOPED(&space->lock);
    ASSERT(dl_contains(&space->head, &rng->dl));

    if (space->table) {
        mmu_unmap(space->table, rng->vaddr, rng->vend);
    }

    pagelist_free(&rng->pages);
    dl_remove(&rng->dl);
}

//------------------------------------------------------------------------------

#ifndef UNIT_TEST

static void vmspace_show() {
    vmspace_t *vm = &g_kernel_vm;
    SPINLOCK_SCOPED(&vm->lock);
    console_printf("kernel vmspace:\n");
    for (dlnode_t *i = vm->head.next; &vm->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        console_printf("vm %-16s %016zx~%016zx -> ", rng->desc, rng->vaddr, rng->vend);
        if (0 == rng->pages.head) {
            console_printf("none\n");
        } else if (0 == rng->pages.tail) {
            console_printf("starting at 0x%x\n", rng->pages.head);
        } else {
            console_printf("blocks");
            for (uint32_t blk = rng->pages.head; blk; blk = g_pages[blk].next) {
                console_printf(" 0x%x:%d", blk, g_pages[blk].rank);
            }
            console_printf("\n");
        }
    }
}

KSHELL_CMD("vm", vmspace_show);

#endif // UNIT_TEST
