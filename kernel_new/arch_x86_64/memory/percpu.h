#ifndef PERCPU_H
#define PERCPU_H

#include <common.h>

// INIT_TEXT size_t percpu_reserve(size_t size, size_t align);
// INIT_TEXT size_t percpu_align_to_l1();
// INIT_TEXT size_t percpu_allocate(size_t va);

INIT_TEXT size_t percpu_nmi_stack_top(int cpu);
INIT_TEXT size_t percpu_df_stack_top(int cpu);
INIT_TEXT size_t percpu_pf_stack_top(int cpu);
INIT_TEXT size_t percpu_mc_stack_top(int cpu);
INIT_TEXT size_t percpu_int_stack_top(int cpu);

INIT_TEXT size_t percpu_init(size_t va);
INIT_TEXT void thiscpu_init(int idx);

#endif // PERCPU_H
