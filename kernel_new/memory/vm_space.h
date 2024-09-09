#ifndef VM_SPACE_H
#define VM_SPACE_H

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

void vm_init(vmspace_t *space);
void vm_insert(vmspace_t *space, vmrange_t *rng);
void vm_show(vmspace_t *space);

#endif // VM_SPACE_H
