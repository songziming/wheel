#include "vmspace.h"
#include <arch_api.h>
#include "page.h"
#include <debug.h>
#include <kshell.h>
#include <console.h>


// 内核地址空间布局
vmspace_t g_kernel_vm;


// 创建新的地址空间，包括内核部分的映射
void vmspace_init(vmspace_t *space, size_t start, size_t end) {
    ASSERT(NULL != space);
    dl_init_circular(&space->head);
    space->lock = SPINLOCK_INIT;
    space->dyn_start = start + PAGE_SIZE - 1;
    space->dyn_start &= ~(PAGE_SIZE - 1);
    space->dyn_end = end & ~(PAGE_SIZE - 1);
}

vmrange_t *vmspace_find(vmspace_t *space, size_t addr) {
    ASSERT(NULL != space);

    vmrange_t *found = NULL;
    { SPINLOCK_SCOPED(&space->lock);
      for (dlnode_t *i = space->head.next; &space->head != i; i = i->next) {
          vmrange_t *rng = containerof(i, vmrange_t, dl);
          if ((rng->vaddr <= addr) && (addr < rng->vend)) {
              found = rng;
              return found;
          }
      }
    }
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

    { SPINLOCK_SCOPED(&space->lock);
      vmspace_insert_nolock(space, rng);
    }
}

// 寻找一段虚拟内存范围，但是不分配
// 找到了返回 1，否则返回 0
static int find_vmrange_no_lock(vmspace_t *space, vmrange_t *rng, size_t size) {
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
            return 1;
        }

        rng->vaddr = rng_end;
        rng->vend = rng_end + size;
    }

    if (rng->vend <= space->dyn_end) {
        return 1;
    }
    return 0;
}

// 划分一段虚拟内存，不映射物理内存
void *vmspace_alloc_nomap(vmspace_t *space, vmrange_t *rng, size_t size) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);

    { SPINLOCK_SCOPED(&space->lock);
      ASSERT(!dl_contains(&space->head, &rng->dl));

      if (0 == find_vmrange_no_lock(space, rng, size)) {
          // 找不到合适的虚拟地址范围，直接退出
          return NULL;
      }

      rng->paddr = 0;
      rng->pages.head = 0;
      rng->pages.tail = 0;
      vmspace_insert_nolock(space, rng);
    }
    return (void*)rng->vaddr;
}

// 在地址空间中寻找一段范围，页对齐，前后留出 guard page
// 并且分配物理内存，在页表中添加映射
void *vmspace_alloc_block(vmspace_t *space, vmrange_t *rng,
        uint32_t rank, page_type_t type, mmu_attr_t attrs) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);

    size_t size = PAGE_SIZE << rank;

    { SPINLOCK_SCOPED(&space->lock);
      ASSERT(!dl_contains(&space->head, &rng->dl));

      if (0 == find_vmrange_no_lock(space, rng, size)) {
          logk("cannot reserve vmrange of rank-%d\n", rank);
          return NULL;
      }

      rng->pages.head = 0;
      rng->pages.tail = 0;
      rng->paddr = page_alloc(rank, type);
      if (0 == rng->paddr) {
          logk("cannot alloc page of rank-%d\n", rank);
          return NULL;
      }

      rng->attrs = attrs;
      mmu_map(space->table, rng->vaddr, rng->vend, rng->paddr, attrs);
      vmspace_insert_nolock(space, rng);
    }
    return (void*)rng->vaddr;
}

void *vmspace_alloc(vmspace_t *space, vmrange_t *rng, size_t size,
        page_type_t type, mmu_attr_t attrs) {
    ASSERT(NULL != space);
    ASSERT(NULL != rng);

    size += PAGE_SIZE - 1;
    size &= ~(PAGE_SIZE - 1);

    {
        SPINLOCK_SCOPED(&space->lock);
        ASSERT(!dl_contains(&space->head, &rng->dl));

        if (0 == find_vmrange_no_lock(space, rng, size)) {
            logk("cannot reserve vmrange of size-0x%zx\n", size);
            return NULL;
        }

        // 如果只申请一个页，则使用块分配接口
        if (PAGE_SIZE == size) {
            rng->paddr = page_alloc(0, type);
            if (0 == rng->paddr) {
                return NULL;
            }
            mmu_map(space->table, rng->vaddr, rng->vend, rng->paddr, attrs);
        } else {
            pagelist_alloc(&rng->pages, size >> PAGE_SHIFT, type);
            // TODO 检查pagelist申请是否成功
            size_t va = rng->vaddr;
            for (uint32_t blk = rng->pages.head; blk; blk = g_pages[blk].next) {
                size_t blksize = PAGE_SIZE << g_pages[blk].rank;
                mmu_map(space->table, va, va + blksize, (size_t)blk << PAGE_SHIFT, attrs);
                va += blksize;
            }
        }

        rng->attrs = attrs;
        vmspace_insert_nolock(space, rng);
    }
    return (void*)rng->vaddr;
}

// TODO 没有检查地址范围是否有冲突
void vmspace_alloc_at(vmspace_t *space, vmrange_t *rng,
        size_t addr, size_t size, page_type_t type, mmu_attr_t attrs) {
    rng->vaddr = addr;
    rng->vend = addr + size;
    rng->attrs = attrs;

    {
        SPINLOCK_SCOPED(&space->lock);
        vmspace_insert_nolock(space, rng);

        if (size <= PAGE_SIZE) {
            rng->paddr = page_alloc(0, type);
            if (0 == rng->paddr) {
                return;
            }
            mmu_map(space->table, rng->vaddr, rng->vend, rng->paddr, attrs);
        } else {
            size += PAGE_SIZE - 1;
            pagelist_alloc(&rng->pages, size >> PAGE_SHIFT, type);
            rng->paddr = 0;
            // TODO 检查pagelist申请是否成功
            size_t va = rng->vaddr;
            for (uint32_t blk = rng->pages.head; blk; blk = g_pages[blk].next) {
                size_t blksize = PAGE_SIZE << g_pages[blk].rank;
                mmu_map(space->table, va, va + blksize, (size_t)blk << PAGE_SHIFT, attrs);
                va += blksize;
            }
        }
    }
}

void *vmspace_alloc_stack(vmspace_t *space, vmrange_t *rng) {
    return vmspace_alloc(space, rng, PAGE_SIZE, PT_STACK, MMU_WRITE);
}

// 映射地址不变，仅改变属性
// TODO 换成更底层的 mmu_remap，仅遍历页表，仅修改属性，不分裂大页
void vmspace_remap(vmspace_t *space, vmrange_t *rng, mmu_attr_t attrs) {
    SPINLOCK_SCOPED(&space->lock);
    ASSERT(dl_contains(&space->head, &rng->dl));

    rng->attrs = attrs;
    tlb_shootdown(rng->vaddr, rng->vend);
    mmu_unmap(space->table, rng->vaddr, rng->vend);
    if (0 != rng->paddr) {
        // 物理地址是连续的
        mmu_map(space->table, rng->vaddr, rng->vend, rng->paddr, attrs);
    } else {
        // 物理地址是不连续的，需要遍历 page-list
        size_t va = rng->vaddr;
        for (uint32_t blk = rng->pages.head; blk; blk = g_pages[blk].next) {
            size_t blksize = PAGE_SIZE << g_pages[blk].rank;
            mmu_map(space->table, va, va + blksize, (size_t)blk << PAGE_SHIFT, attrs);
            va += blksize;
        }
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

    if (0 != rng->paddr) {
        page_free(rng->paddr);
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
        console_printf("  - vm %016zx~%016zx -> pa %8zx : %s\n",
            rng->vaddr, rng->vend, rng->paddr, rng->desc);
    }
}

KSHELL_CMD("vm", vmspace_show);

#endif // UNIT_TEST
