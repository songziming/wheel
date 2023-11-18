#ifndef ARCH_IMPL_H
#define ARCH_IMPL_H

#include <def.h>

int arch_unwind(void **addrs, int max, uint64_t rbp);

#endif // ARCH_IMPL_H
