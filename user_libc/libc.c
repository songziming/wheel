#include "libc.h"
#include <syscall.h>

size_t strlen(const char *s) {
    size_t i = 0;
    for (; s[i]; ++i) {}
    return i;
}
