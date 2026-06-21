#ifndef LIBC_H
#define LIBC_H

#include <stdint.h>

uint64_t _syscall(uint64_t rdi, uint64_t rsi);
void sys_print(const char *s);

#endif // LIBC_H
