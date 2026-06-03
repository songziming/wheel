#ifndef VMSPACE_H
#define VMSPACE_H

#include <spin.h>
#include "page.h"
#include <dllist.h>
#include <arch_api.h>

// 代表一段虚拟地址范围
typedef struct vmrange {
    dlnode_t    dl;
    size_t      vaddr;
    size_t      vend;
    size_t      paddr;  // 非零表示映射到连续的物理内存
    pglist_t    pages;  // 映射到不连续的物理内存
    mmu_attr_t  attrs;
    const char *desc;
} vmrange_t;

// 代表一个虚拟地址空间
typedef struct vmspace {
    spin_t   lock;
    // size_t   start; // 管理范围开始
    // size_t   end;   // 管理范围结束
    dlnode_t head;  // vmrange 链表头节点
    size_t   table; // 页表
} vmspace_t;


extern vmspace_t g_kernel_vm;


void vmspace_init(vmspace_t *space);
vmrange_t *vmspace_find(vmspace_t *space, size_t addr);
void vmspace_insert(vmspace_t *space, vmrange_t *rng);

// 仅申请一段虚拟内存，不映射物理内存
size_t vmspace_valloc(vmspace_t *space, vmrange_t *rng,
        size_t start, size_t end, size_t size);

// 分配连续的物理内存
size_t vmspace_alloc(vmspace_t *space, vmrange_t *rng,
        size_t start, size_t end, uint32_t rank,
        page_type_t type, mmu_attr_t attrs);
size_t vmspace_alloc_stack(vmspace_t *space, vmrange_t *rng, uint32_t rank);

// 分配不连续的物理内存，映射到连续的虚拟地址范围
size_t vmspace_alloc_sparse(vmspace_t *space, vmrange_t *rng,
        size_t start, size_t end, size_t size,
        page_type_t type, mmu_attr_t attrs);

void vmspace_remove(vmspace_t *space, vmrange_t *rng);

#endif // VMSPACE_H
