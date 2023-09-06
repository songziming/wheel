#ifndef ARCH_API_P_H
#define ARCH_API_P_H

#include <base.h>

int unwind_from(void **addrs, int max, uint64_t rbp);

#endif // ARCH_API_P_H
