#include "libc.h"

void sys_print(const char *s) {
    _syscall(123, (uint64_t)s);
}
