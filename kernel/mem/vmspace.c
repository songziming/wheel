#include "vmspace.h"
#include <arch_api.h>
#include "page.h"
#include <debug.h>
#include <kshell.h>
#include <console.h>


// 内核地址空间布局
vmspace_t g_kernel_vm;


// 创建新的地址空间，包括内核部分的映射
void vmspace_init(vmspace_t *space) {
    ASSERT(NULL != space);
    space->lock = SPIN_INIT;
    dl_init_circular(&space->head);
}

vmrange_t *vmspace_find(vmspace_t *space, size_t addr) {
    ASSERT(NULL != space);

    int key = irq_spin_take(&space->lock);
    for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        if ((rng->vaddr <= addr) && (addr < rng->vend)) {
            irq_spin_give(&space->lock, key);
            return rng;
        }
    }

    irq_spin_give(&space->lock, key);
    return NULL;
}

// 在地址空间中添加一个范围，不操作物理地址
static void vmspace_insert_nolock(vmspace_t *space, vmrange_t *rng) {
    ASSERT(!dl_contains(&space->head, &rng->dl));

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

// 在地址空间中添加一个范围，不操作物理地址
void vmspace_insert(vmspace_t *space, vmrange_t *rng) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);
    ASSERT(rng->vaddr < rng->vend);
    ASSERT(0 == (rng->vaddr & (PAGE_SIZE - 1)));

    int key = irq_spin_take(&space->lock);
    vmspace_insert_nolock(space, rng);
    irq_spin_give(&space->lock, key);
}

// 寻找一段虚拟内存范围，但是不分配
// 找到了返回 1，否则返回 0
static int find_vmrange_no_lock(vmspace_t *space, vmrange_t *rng,
        size_t start, size_t end, size_t size) {
    rng->vaddr = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
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
            return 1;
        }

        rng->vaddr = rng_end;
        rng->vend = rng_end + size;
    }

    if (rng->vend <= end) {
        return 1;
    }
    return 0;
}

// 划分一段虚拟内存，不映射物理内存
size_t vmspace_valloc(vmspace_t *space, vmrange_t *rng,
        size_t start, size_t end, size_t size) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);
    ASSERT(0 != start);
    ASSERT(start < end);

    int key = irq_spin_take(&space->lock);
    ASSERT(!dl_contains(&space->head, &rng->dl));

    if (0 == find_vmrange_no_lock(space, rng, start, end, size)) {
        // 找不到合适的虚拟地址范围，直接退出
        irq_spin_give(&space->lock, key);
        return 0;
    }

    rng->paddr = 0;
    rng->pages.head = 0;
    rng->pages.tail = 0;
    vmspace_insert_nolock(space, rng);
    irq_spin_give(&space->lock, key);
    return rng->vaddr;
}

// 在地址空间中寻找一段范围，页对齐，前后留出 guard page
// 并且分配物理内存，在页表中添加映射
size_t vmspace_alloc(vmspace_t *space, vmrange_t *rng, size_t start, size_t end,
        uint32_t rank, page_type_t type, mmu_attr_t attrs) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);
    ASSERT(0 != start);
    ASSERT(start < end);

    size_t size = PAGE_SIZE << rank;

    int key = irq_spin_take(&space->lock);
    ASSERT(!dl_contains(&space->head, &rng->dl));

    if (0 == find_vmrange_no_lock(space, rng, start, end, size)) {
        logk("cannot reserve vmrange of rank-%d\n", rank);
        irq_spin_give(&space->lock, key);
        return 0;
    }

    rng->pages.head = 0;
    rng->pages.tail = 0;
    rng->paddr = PAGE_ALLOC(rank, type);
    if (0 == rng->paddr) {
        logk("cannot alloc page of rank-%d\n", rank);
        irq_spin_give(&space->lock, key);
        return 0;
    }

    rng->attrs = attrs;
    mmu_map(space->table, rng->vaddr, rng->vend, rng->paddr, attrs);
    vmspace_insert_nolock(space, rng);
    irq_spin_give(&space->lock, key);
    return rng->vaddr;
}

size_t vmspace_alloc_stack(vmspace_t *space, vmrange_t *rng, uint32_t rank) {
    return vmspace_alloc(space, rng,
        STACK_ZONE_START, STACK_ZONE_END,
        rank, PT_STACK, MMU_WRITE);
}

size_t vmspace_alloc_sparse(vmspace_t *space, vmrange_t *rng,
        size_t start, size_t end, size_t size,
        page_type_t type, mmu_attr_t attrs) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);
    ASSERT(0 != start);
    ASSERT(start < end);

    size += PAGE_SIZE - 1;
    size &= ~(PAGE_SIZE - 1);

    int key = irq_spin_take(&space->lock);
    ASSERT(!dl_contains(&space->head, &rng->dl));

    if (0 == find_vmrange_no_lock(space, rng, start, end, size)) {
        logk("cannot reserve vmrange of size-0x%zx\n", size);
        irq_spin_give(&space->lock, key);
        return 0;
    }

    pagelist_alloc(&rng->pages, size >> PAGE_SHIFT, type, __FILE__, __LINE__);
    size_t va = rng->vaddr;
    for (uint32_t blk = rng->pages.head; blk; blk = g_pages[blk].next) {
        size_t blksize = PAGE_SIZE << g_pages[blk].rank;
        mmu_map(space->table, va, va + blksize, (size_t)blk << PAGE_SHIFT, attrs);
        va += blksize;
    }

    vmspace_insert_nolock(space, rng);
    irq_spin_give(&space->lock, key);
    return rng->vaddr;
}

void vmspace_remove(vmspace_t *space, vmrange_t *rng) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);

    int key = irq_spin_take(&space->lock);
    ASSERT(dl_contains(&space->head, &rng->dl));

    if (space->table) {
        mmu_unmap(space->table, rng->vaddr, rng->vend);
    }

    if (0 != rng->paddr) {
        page_free(rng->paddr);
    }
    pagelist_free(&rng->pages);

    dl_remove(&rng->dl);
    irq_spin_give(&space->lock, key);
}

//------------------------------------------------------------------------------

#if !defined(UNIT_TEST)

static void vmspace_show() {
    vmspace_t *vm = &g_kernel_vm;
    int key = irq_spin_take(&vm->lock);
    console_printf("kernel vmspace:\n");
    for (dlnode_t *i = vm->head.next; &vm->head != i; i = i->next) {
        vmrange_t *rng = containerof(i, vmrange_t, dl);
        console_printf("  - vm %016zx~%016zx -> pa %8zx : %s\n",
            rng->vaddr, rng->vend, rng->paddr, rng->desc);
    }
    irq_spin_give(&vm->lock, key);
}

KSHELL_CMD("vm", vmspace_show);

#endif // !defined(UNIT_TEST)
