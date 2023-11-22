#ifndef ARCH_API_H
#define ARCH_API_H

#include "def.h"

void cpu_halt();
void cpu_pause();
void cpu_rfence();
void cpu_wfence();
void cpu_rwfence();

void emu_break();
void emu_exit(int ret);

void *early_alloc_ro(size_t size);
void *early_alloc_rw(size_t size);

int unwind(size_t *addrs, int max);

int cpu_count();
int cpu_index();

void *pcpu_ptr(int idx, void *ptr);
void *this_ptr(void *ptr);

#endif // ARCH_API_H
