#ifndef BASE_DEF_H
#define BASE_DEF_H

// freestanding headers
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>

#define UNUSED      __attribute__((unused))
#define PACKED      __attribute__((packed))
#define WEAK        __attribute__((weak))
#define PURE        __attribute__((pure))
#define NORETURN    __attribute__((noreturn))
#define ALIGNED(x)  __attribute__((aligned(x)))
#define SECTION(x)  __attribute__((section(x)))
#define CONST       SECTION(".rodata")
#define INIT_TEXT   SECTION(".init.text")
#define INIT_DATA   SECTION(".init.data")
#define INIT_BSS    SECTION(".init.bss")
#define PCPU_DATA   SECTION(".pcpu.data")
#define PCPU_BSS    SECTION(".pcpu.bss")
#define PRINTF(s,a) __attribute__((format(printf,s,a)))

#ifndef offsetof
#define offsetof(t,m) ((size_t)&((t *)0)->m)
#endif

#ifndef containerof
#define containerof(p,t,m) ((t *)((size_t)p - offsetof(t,m)))
#endif

// 使用 32-bit 整型表示页号，最多支持 4K-1 个物理页
typedef uint32_t pfn_t;
#define INVALID_PFN  0xffffffffU
#define INVALID_ADDR 0xffffffffffffffffUL

// 最多支持 64 个 CPU，cpuset 定义为数组可以支持更多
typedef uint64_t cpuset_t;
#define MAX_CPU_NUM 64

#endif // BASE_DEF_H