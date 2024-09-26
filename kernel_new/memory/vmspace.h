#ifndef VMSPACE_H
#define VMSPACE_H

#include <arch_intf.h>
#include <library/dllist.h>
#include <memory/page.h>

typedef struct vmrange {
    dlnode_t    dl;
    size_t      addr;   // 虚拟地址开始（页对齐）
    size_t      end;    // 虚拟地址结束
    size_t      pa;     // 映射的物理地址（页对齐）
    mmu_attr_t  attrs;  // 映射属性
    const char *desc;
} vmrange_t;

typedef struct vmspace {
    dlnode_t head;   // vmrange 链表头节点
    size_t  table;  // 页表
} vmspace_t;

INIT_TEXT void kernel_vmspace_init();
vmspace_t *kernel_vmspace();

void vmspace_create(vmspace_t *space);

vmrange_t *vmspace_locate(vmspace_t *space, size_t addr);
void vmspace_insert(vmspace_t *space, vmrange_t *rng);
void vmspace_remove(vmspace_t *space, vmrange_t *rng);

void vmspace_show(vmspace_t *space);

size_t vmspace_alloc(vmspace_t *space, vmrange_t *rng, size_t start, size_t end,
    int rank, page_type_t type, mmu_attr_t attrs);
size_t alloc_kernel_stack(vmrange_t *rng, int rank);

#endif // VMSPACE_H
