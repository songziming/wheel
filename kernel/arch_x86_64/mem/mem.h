#ifndef ARCH_X86_64_MEM_MEM_H
#define ARCH_X86_64_MEM_MEM_H

#include <vmspace.h>

// typedef struct vmrange vmrange_t;
extern vmspace_t g_kernel_vm;

INIT_TEXT void kspace_add(vmrange_t *rng, size_t va, size_t end, const char *desc, mmu_attr_t attrs);

INIT_TEXT size_t percpu_init(size_t va);
INIT_TEXT void thiscpu_init(int idx);

INIT_TEXT size_t get_int_top(int cpu);
INIT_TEXT size_t get_ist_nmi(int cpu);
INIT_TEXT size_t get_ist_df(int cpu);
INIT_TEXT size_t get_ist_pf(int cpu);
INIT_TEXT size_t get_ist_mc(int cpu);

INIT_TEXT void mem_init();

#endif // ARCH_X86_64_MEM_MEM_H
