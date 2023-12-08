#ifndef MEM_VMSPACE_H
#define MEM_VMSPACE_H

#include <base.h>
#include <spin.h>
#include <dllist.h>

typedef struct vmspace {
    dlnode_t dl_head;
    spin_t   lock;
} vmspace_t;

// 代表一段虚拟地址范围，以及映射的物理地址
// 可以按需映射，发生 #PF 时映射用到的页
typedef struct vmrange {
    size_t      va;     // 虚拟空间起始地址，页对齐
    size_t      size;   // 虚拟空间大小，页对齐
    uint32_t    flags;
    const char *desc;
    dlnode_t    dl;
} vmrange_t;

typedef enum range_flag {
    RF_DOWN = 1, // 向低地址增长，也按这个方向映射物理页
    RF_GUARD = 2, // 首尾保留 guard page
    RF_MAPPED = 4, // 已映射到物理页
} range_flag_t;


void vmspace_init(vmspace_t *vm);

void vmspace_insert_range(vmspace_t *vm, vmrange_t *new_rng);
void vmspace_remove_range(vmspace_t *vm, vmrange_t *rng);
vmrange_t *vmspace_locate_range(vmspace_t *vm, size_t va);

size_t vmspace_reserve(vmspace_t *vm, size_t size, vmrange_t *rng, size_t start, size_t end, range_flag_t flags);

void vmspace_show(vmspace_t *vm);

#endif // MEM_VMSPACE_H
