#ifndef ARCH_PRIVATE_H
#define ARCH_PRIVATE_H

// 供 arch 模块内部使用的函数，不对外公开

#include <base.h>

// 调试输出
void serial_console_puts(const char *s, size_t n);
void serial_framebuf_puts(const char *s, size_t n);

// 堆栈展开
int unwind_from(void **addrs, int max, uint64_t rbp);

// 分页
void mmu_show(uint64_t tbl);

#endif // ARCH_PRIVATE_H
