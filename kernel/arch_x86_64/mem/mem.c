#include "mem.h"
#include <arch_api.h>
#include <vmspace.h>


static vmspace_t g_kernel_vm;


INIT_TEXT void kspace_init() {
    vmspace_init(&g_kernel_vm);
}

INIT_TEXT size_t kspace_add(vmrange_t *rng, size_t va, size_t len, const char *desc) {
    va += PAGE_SIZE - 1;
    va &= ~(PAGE_SIZE - 1);
    rng->vaddr = va;
    rng->vend  = va + len;
    rng->desc  = desc;
    vmspace_insert(&g_kernel_vm, rng);
    return rng->vend;
}

INIT_TEXT void add_kernel_vmrange(vmrange_t *rng, size_t va, size_t len, const char *desc) {
    rng->vaddr = va;
    rng->vend  = va + len;
    rng->desc  = desc;
}
