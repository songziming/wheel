#ifndef BASE_ARCH_H
#define BASE_ARCH_H

// 每个 arch 模块都要实现的接口

#include <base_def.h>
#include <arch_config.h>

// 启动阶段永久分配内存
INIT_TEXT void *early_alloc_ro(size_t size);
INIT_TEXT void *early_alloc_rw(size_t size);

unsigned cpu_count();

#endif // BASE_ARCH_H
