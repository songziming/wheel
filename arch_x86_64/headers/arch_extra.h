#ifndef ARCH_EXTRA_H
#define ARCH_EXTRA_H

#include <defs.h>

// 有些函数没有在 arch_api.h 里定义，因为参数列表可能不同

int unwind_from(void **addrs, int max, uint64_t rbp);

#endif // ARCH_EXTRA_H
