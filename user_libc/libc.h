#ifndef LIBC_H
#define LIBC_H

// 系统调用的包装

#include <syscall.h>

size_t strlen(const char *s);

static inline void sys_exit(int ret) { __syscall1(SYS_exit, ret); }
static inline void sys_print(const char *s) { __syscall3(SYS_write, 1, (size_t)s, strlen(s)); }
static inline void sys_read(char *s, size_t len) { __syscall3(SYS_read, 0, (size_t)s, len); }

#endif // LIBC_H
