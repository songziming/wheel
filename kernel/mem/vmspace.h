#ifndef VMSPACE_H
#define VMSPACE_H

#include <dllist.h>

// 代表一段虚拟地址范围
typedef struct vmrange {
    dlnode_t    dl;
    size_t      vaddr;
    size_t      vend;
    const char *desc;
} vmrange_t;

// 代表一个虚拟地址空间
typedef struct vmspace {
    dlnode_t head;   // vmrange 链表头节点
    size_t  table;  // 页表
} vmspace_t;

void vmspace_init(vmspace_t *space);
vmrange_t *vmspace_find(vmspace_t *space, size_t addr);
void vmspace_insert(vmspace_t *space, vmrange_t *rng);
void vmspace_show(vmspace_t *space);

#endif // VMSPACE_H