#include "mem.h"
#include <arch_api.h>
#include <vmspace.h>
#include <early_alloc.h>
#include <debug.h>


static vmspace_t g_kernel_vm;



INIT_TEXT size_t kspace_add(vmrange_t *rng, size_t va, size_t len, const char *desc) {
    va += PAGE_SIZE - 1;
    va &= ~(PAGE_SIZE - 1);
    rng->vaddr = va;
    rng->vend  = va + len;
    rng->desc  = desc;
    vmspace_insert(&g_kernel_vm, rng);
    return rng->vend;
}


// 初始化内存布局，规划内核虚拟地址空间
INIT_TEXT void mem_init() {
    // 不再使用 early-alloc
    size_t ro_end = (size_t)early_alloc_ro(0);
    size_t rw_end = (size_t)early_alloc_rw(0);
    early_alloc_disable();
    logk("ro_end=0x%lx, rw_end=0x%lx\n", ro_end, rw_end);

    // 记录内核的虚拟地址空间布局
    vmspace_init(&g_kernel_vm);

    // 内核结尾部分用作 percpu
    rw_end = percpu_init(rw_end);

    logk("kernel vmspace layout:\n");
    vmspace_show(&g_kernel_vm);
}
