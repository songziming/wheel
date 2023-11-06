#ifndef ARCH_X86_64_LIBA_ATOMIC_H
#define ARCH_X86_64_LIBA_ATOMIC_H

#include <base.h>

// all functions return the old value of p

extern s32 atomic32_get(s32 * p);
extern s32 atomic32_set(s32 * p, s32 x);
extern s32 atomic32_inc(s32 * p);
extern s32 atomic32_dec(s32 * p);
extern s32 atomic32_cas(s32 * p, s32 x, s32 y);

extern s64 atomic64_get(s64 * p);
extern s64 atomic64_set(s64 * p, s64 x);
extern s64 atomic64_inc(s64 * p);
extern s64 atomic64_dec(s64 * p);
extern s64 atomic64_cas(s64 * p, s64 x, s64 y);

#define atomic_get(p)       atomic64_get((s64 *) p)
#define atomic_set(p, x)    atomic64_set((s64 *) p, (s64) x)
#define atomic_inc(p)       atomic64_inc((s64 *) p)
#define atomic_dec(p)       atomic64_dec((s64 *) p)
#define atomic_cas(p, x, y) atomic64_cas((s64 *) p, (s64) x, (s64) y)

#endif // ARCH_X86_64_LIBA_ATOMIC_H
