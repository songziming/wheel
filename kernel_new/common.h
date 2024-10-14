#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>

#define UNUSED          __attribute__((unused))
#define PACKED          __attribute__((packed))
#define PURE            __attribute__((pure))
#define NORETURN        __attribute__((noreturn))
#define MALLOC          __attribute__((malloc))
#define WEAKALIAS(name) __attribute__((weak, alias(name)))
#define PRINTF(s,a)     __attribute__((format(printf,s,a)))
#define ALIGNED(x)      __attribute__((aligned(x)))
#define SECTION(x)      __attribute__((section(x)))

// #ifdef UNIT_TEST
// #define CONST
// #define INIT_TEXT
// #define INIT_DATA
// #define INIT_BSS
// #define PERCPU_DATA
// #define PERCPU_BSS
// #else
#define CONST           SECTION(".rodata")
#define INIT_TEXT       SECTION(".init.text")
#define INIT_DATA       SECTION(".init.data")
#define INIT_BSS        SECTION(".init.bss")
#define PERCPU_DATA     SECTION(".percpu.data")
#define PERCPU_BSS      SECTION(".percpu.bss")
// #endif

#ifndef offsetof
#define offsetof(t,m) ((size_t)&((t *)0)->m)
#endif

#ifndef containerof
#define containerof(p,t,m) ((t *)((size_t)(p) - offsetof(t,m)))
#endif

// 使用 uint64 表示 CPU 位图，最多支持 64 个核心
typedef uint64_t cpuset_t;
#define MAX_CPU_COUNT 64

// 使用 uint32 表示页号，最多支持 4G-1 个物理页
typedef uint32_t pfn_t;
#define INVALID_PFN  0xffffffffU
// #define INVALID_ADDR 0xffffffffffffffffUL

// 等待时间，单位是 tick
#define NOWAIT -1
#define FOREVER -2

#endif // COMMON_H
