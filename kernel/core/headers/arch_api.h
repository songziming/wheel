#ifndef ARCH_API_H
#define ARCH_API_H

// 每个 arch 模块都要实现的接口

#include <base_def.h>

void cpu_halt();
void cpu_pause();
void cpu_rfence();
void cpu_wfence();
void cpu_rwfence();

int unwind(void **addrs, int max);
void emulator_exit(int ret);

// 启动阶段永久分配内存
INIT_TEXT void *early_alloc_ro(size_t size);
INIT_TEXT void *early_alloc_rw(size_t size);

int cpu_count();
int cpu_index();

void *pcpu_ptr(int idx, void *ptr);
void *this_ptr(void *ptr);

#endif // ARCH_API_H
