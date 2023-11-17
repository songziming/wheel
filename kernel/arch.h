#ifndef ARCH_H
#define ARCH_H

#include "def.h"

void *early_alloc_ro(size_t size);
void *early_alloc_rw(size_t size);

void cpu_halt();
void cpu_pause();
void cpu_rfence();
void cpu_wfence();
void cpu_rwfence();

int unwind(void **addrs, int max);
void vmshutdown(int ret);

int cpu_count();
int cpu_index();

void *pcpu_ptr(int idx, void *ptr);
void *this_ptr(void *ptr);

#endif // ARCH_H
