#ifndef ARCH_API_COMMON_H
#define ARCH_API_COMMON_H

#include <stddef.h>
#include <stdint.h>

// 需要 arch 实现的函数

int cpu_count();
int cpu_index();

void *percpu_ptr(int idx, void *p);
#define PERCPU(i,x) ((__typeof__(x))percpu_ptr(i,x))

#endif // ARCH_API_COMMON_H
