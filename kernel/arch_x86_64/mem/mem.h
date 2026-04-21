#ifndef ARCH_X86_64_MEM_MEM_H
#define ARCH_X86_64_MEM_MEM_H

#include <vmspace.h>

INIT_TEXT void kspace_add(vmrange_t *rng, size_t va, size_t end, const char *desc, mmu_attr_t attrs);

INIT_TEXT size_t percpu_init(size_t va);
INIT_TEXT void thiscpu_init(int idx);

INIT_TEXT size_t thiscpu_nmi_stack();
INIT_TEXT size_t thiscpu_df_stack();
INIT_TEXT size_t thiscpu_pf_stack();
INIT_TEXT size_t thiscpu_mc_stack();
INIT_TEXT size_t thiscpu_int_stack();

INIT_TEXT void mem_init();

#endif // ARCH_X86_64_MEM_MEM_H
