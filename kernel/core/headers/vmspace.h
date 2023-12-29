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


// extern vmspace_t g_kernel_proc.space;


void vmspace_init(vmspace_t *vm);
void vmrange_init(vmrange_t *rng, size_t addr, size_t end, const char *desc);

void vmspace_insert(vmspace_t *vm, vmrange_t *rng);
void vmspace_remove(vmspace_t *vm, vmrange_t *rng);

vmrange_t *vmspace_locate(vmspace_t *vm, size_t va);
size_t vmspace_search(vmspace_t *vm, size_t start, size_t end, size_t size);

void vmspace_show(vmspace_t *vm);

#endif // VMSPACE_H
