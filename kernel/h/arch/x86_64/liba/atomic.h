#ifndef ARCH_X86_64_LIBA_ATOMIC_H
#define ARCH_X86_64_LIBA_ATOMIC_H

#include <base.h>

// all functions return the old value of p

extern u32 atomic32_get(u32 * p);
extern u32 atomic32_set(u32 * p, u32 x);
extern u32 atomic32_inc(u32 * p);
extern u32 atomic32_dec(u32 * p);
extern u32 atomic32_cas(u32 * p, u32 x, u32 y);

extern u64 atomic64_get(u64 * p);
extern u64 atomic64_set(u64 * p, u64 x);
extern u64 atomic64_inc(u64 * p);
extern u64 atomic64_dec(u64 * p);
extern u64 atomic64_cas(u64 * p, u64 x, u64 y);

#define atomicul_get(p)       atomic64_get((u64 *) p)
#define atomicul_set(p, x)    atomic64_set((u64 *) p, (u64) x)
#define atomicul_inc(p)       atomic64_inc((u64 *) p)
#define atomicul_dec(p)       atomic64_dec((u64 *) p)
#define atomicul_cas(p, x, y) atomic64_cas((u64 *) p, (u64) x, (u64) y)

#endif // ARCH_X86_64_LIBA_ATOMIC_H
