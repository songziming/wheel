#ifndef LIBC_H
#define LIBC_H

// 系统调用的包装

#include <syscall.h>

static inline void sys_print(const char *s) { __syscall1(123, (int64_t)s); }
static inline void sys_exit(int ret) { __syscall1(SYS_exit, ret); }

#endif // LIBC_H
