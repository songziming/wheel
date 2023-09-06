#ifndef ARCH_API_H
#define ARCH_API_H

// 每个 arch 模块都要实现的接口

#include <base_def.h>

void cpu_halt();
void cpu_pause();
void cpu_rfence();
void cpu_wfence();
void cpu_rwfence();

// 启动阶段永久分配内存
INIT_TEXT void *early_alloc_ro(size_t size);
INIT_TEXT void *early_alloc_rw(size_t size);

unsigned cpu_count();
unsigned cpu_index();

int unwind(void **addrs, int max);
void emulator_exit(int ret);

#endif // ARCH_API_H
