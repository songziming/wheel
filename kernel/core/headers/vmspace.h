#ifndef VMSPACE_H
#define VMSPACE_H

#include <def.h>
#include <dllist.h>

typedef struct vmrange {
    dlnode_t    dl;
    size_t      addr;   // 虚拟地址
    size_t      end;
    const char *desc;
} vmrange_t;

typedef struct vmspace {
    dlnode_t    head;
} vmspace_t;

void vmspace_init(vmspace_t *vm);
void vmspace_add(vmspace_t *vm, vmrange_t *rng);
void vmspace_show(vmspace_t *vm);

#endif // VMSPACE_H
