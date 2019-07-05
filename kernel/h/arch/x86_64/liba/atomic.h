#ifndef ARCH_X86_64_LIBA_ATOMIC_H
#define ARCH_X86_64_LIBA_ATOMIC_H

#include <base.h>

// all functions return the old value of p
extern u32 atomic32_get(u32 * p);
extern u32 atomic32_set(u32 * p, u32 x);
extern u32 atomic32_inc(u32 * p);
extern u32 atomic32_dec(u32 * p);
extern u32 atomic32_cas(u32 * p, u32 x, u32 y);

#endif // ARCH_X86_64_LIBA_ATOMIC_H
