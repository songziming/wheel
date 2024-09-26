#ifndef VMSPACE_H
#define VMSPACE_H

#include <library/dllist.h>
#include <arch_intf.h>

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
} vmspace_t;

void vmspace_init(vmspace_t *space);
void vmspace_insert(vmspace_t *space, vmrange_t *rng);
size_t vmspace_alloc(vmspace_t *space, vmrange_t *rng, size_t start, size_t end, size_t size);
vmrange_t *vmspace_locate(vmspace_t *space, size_t addr);
void vmspace_remove(vmspace_t *space, vmrange_t *rng);
void vmspace_show(vmspace_t *space);

#endif // VMSPACE_H
