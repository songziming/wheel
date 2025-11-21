#ifndef VMSPACE_H
#define VMSPACE_H

#include <dllist.h>

// 代表一段虚拟地址范围
typedef struct vmrange {
    dlnode_t    dl;
    size_t      vaddr;
    size_t      vend;
    const      *desc;
} vmrange_t;

// 代表一个虚拟地址空间
typedef struct vmspace {
    dlnode_t head;   // vmrange 链表头节点
    size_t  table;  // 页表
} vmspace_t;

#endif // VMSPACE_H