#ifndef ARCH_x86_64_EXTRA_H
#define ARCH_x86_64_EXTRA_H

#include <def.h>

int arch_unwind(void **addrs, int max, uint64_t rbp);

#endif // ARCH_x86_64_EXTRA_H
