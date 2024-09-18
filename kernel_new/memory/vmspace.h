#ifndef VMSPACE_H
#define VMSPACE_H

#include <library/dllist.h>

typedef struct vmrange {
    dlnode_t dl;
    size_t  addr;
    size_t  end;
    const char *desc;
} vmrange_t;

typedef struct vmspace {
    dlnode_t head;   // vmrange 链表头节点
} vmspace_t;

void vmspace_init(vmspace_t *space);
void vmspace_insert(vmspace_t *space, vmrange_t *rng);
void vmspace_show(vmspace_t *space);

#endif // VMSPACE_H
