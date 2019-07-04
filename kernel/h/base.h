#ifndef BASE_H
#define BASE_H

#include <stddef.h>
#include <stdarg.h>

//------------------------------------------------------------------------------
// type definitions

typedef   signed char           s8;
typedef   signed short          s16;
typedef   signed int            s32;
typedef   signed long long      s64;
typedef   signed long           ssize;

typedef unsigned char           u8;
typedef unsigned short          u16;
typedef unsigned int            u32;
typedef unsigned long long      u64;
typedef unsigned long           usize;

typedef u32                     pfn_t;          // limite: at most 2^32 pages
typedef u64                     cpuset_t;       // limite: at most 64 cpus

//------------------------------------------------------------------------------
// constants

#define NO_ADDR                 ((usize) -1)    // invalid address
#define NO_PAGE                 ((pfn_t) -1)    // invalid page number
#define OK                      ((int)    0)    // return code
#define ERROR                   ((int)   -1)    // return code
#define YES                     ((int)    1)    // boolean value
#define NO                      ((int)    0)    // boolean value

//------------------------------------------------------------------------------
// compiler directives

#define __UNUSED                __attribute__((unused))
#define __PACKED                __attribute__((packed))
#define __ALIGNED(x)            __attribute__((aligned(x)))
#define __SECTION(x)            __attribute__((section(x)))
#define __INIT                  __SECTION(".init.text")
#define __INITDATA              __SECTION(".init.data")
#define __PERCPU                __SECTION(".percpu")
#define __WEAK                  __attribute__((weak))
#define __PURE                  __attribute__((pure))
#define __NORETURN              __attribute__((noreturn))

#define OFFSET(type, field)     __builtin_offsetof(type, field)
#define PARENT(x, type, field)  ((type *) ((u8 *) (x) - OFFSET(type, field)))

//------------------------------------------------------------------------------
// helper macros

#define ASM(...)                __asm__ volatile(__VA_ARGS__)
#define TYPE(x)                 __typeof__(x)

#define CLZ32(x)                __builtin_clz(x)
#define CTZ32(x)                __builtin_ctz(x)
#define CLZ64(x)                __builtin_clzll(x)
#define CTZ64(x)                __builtin_ctzll(x)
#define MAX(x, y)               (((x) > (y)) ? (x) : (y))
#define MIN(x, y)               (((x) < (y)) ? (x) : (y))

#define ROUND_UP(x, align)      (((usize) (x) + (align-1)) & ~(align-1))
#define ROUND_DOWN(x, align)    ( (usize) (x)              & ~(align-1))
#define IS_ALIGNED(x, align)    (((usize) (x) & (align-1)) == 0)

#endif // BASE_H
