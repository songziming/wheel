#ifndef ARCH_API_P_H
#define ARCH_API_P_H

#include <arch_api.h>

int arch_unwind(size_t *addrs, int max, uint64_t rbp);

#endif // ARCH_API_P_H
