#ifndef VMSPACE_H
#define VMSPACE_H

#include <base.h>
#include <dllist.h>

// 记录每一段虚拟内存，组成链表
typedef struct vmrange {
    size_t      addr;   // 虚拟地址起始
    size_t      size;
    pfn_t       page;   // 映射到的第一个物理页块
    const char *desc;
    dlnode_t    dl;
} vmrange_t;

typedef struct vmspace {
    dlnode_t    head;
} vmspace_t;

void vmspace_init(vmspace_t *vm);
void vmspace_insert_range(vmspace_t *vm, vmrange_t *rng);
void vmspace_remove_range(vmspace_t *vm, vmrange_t *rng);
#ifdef DEBUG
void vmspace_show(vmspace_t *vm);
#endif

#endif // VMSPACE_H
