#ifndef VMSPACE_H
#define VMSPACE_H

#include <def.h>
#include <arch_api.h>
#include <dllist.h>

typedef struct vmrange {
    dlnode_t    dl;
    size_t      addr;   // 虚拟地址
    size_t      end;    // 虚拟地址结束
    size_t      pa;     // 映射的物理地址
    mmu_attr_t  attrs;  // 映射属性
    const char *desc;
} vmrange_t;

typedef struct vmspace {
    dlnode_t    head;
} vmspace_t;

void vmspace_init(vmspace_t *vm);
vmspace_t *get_kernel_vmspace();

void vmspace_insert(vmspace_t *vm, vmrange_t *rng, size_t addr, size_t end, const char *desc);
void vmspace_remove(vmspace_t *vm, vmrange_t *rng);

vmrange_t *vmspace_locate(vmspace_t *vm, size_t va);
size_t vmspace_search(vmspace_t *vm, size_t start, size_t end, size_t size);

void vmspace_show(vmspace_t *vm);

#endif // VMSPACE_H
