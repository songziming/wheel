#include "libc.h"
#include <syscall.h>

void sys_print(const char *s) {
    __syscall1(123, (int64_t)s);
}
