#ifndef PERCPU_H
#define PERCPU_H

#include <common.h>

// INIT_TEXT size_t percpu_reserve(size_t size, size_t align);
// INIT_TEXT size_t percpu_align_to_l1();
// INIT_TEXT size_t percpu_allocate(size_t va);

INIT_TEXT size_t percpu_init(size_t addr);

INIT_TEXT size_t get_nmi_stack(int cpu);
INIT_TEXT size_t get_pf_stack(int cpu);
INIT_TEXT size_t get_df_stack(int cpu);
INIT_TEXT size_t get_mc_stack(int cpu);
INIT_TEXT size_t get_int_stack(int cpu);

INIT_TEXT void percpu_reclaim_guard();

INIT_TEXT void gsbase_init(int idx);

#endif // PERCPU_H
