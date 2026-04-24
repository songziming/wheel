#ifndef WHEEL_H
#define WHEEL_H

#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#include <stdint.h>
// #include <stdbool.h>
#include <stdatomic.h>

#define UNUSED      __attribute__((unused))
#define PACKED      __attribute__((packed))
#define PURE        __attribute__((pure))
#define WEAK        __attribute__((weak))
#define NORETURN    __attribute__((noreturn))
#define MALLOC      __attribute__((malloc))
#define PRINTF(s,a) __attribute__((format(printf,s,a)))
#define ALIGNED(x)  __attribute__((aligned(x)))

#if defined(UNIT_TEST)
    #define ASMV(...)
    #define SECTION(x)
#else
    // always use volatile for safety
    #define ASMV        __asm__ volatile
    #define SECTION(x)  __attribute__((section(x)))
#endif

#define CONST       SECTION(".rodata")
#define INIT_TEXT   SECTION(".init.text")
#define INIT_DATA   SECTION(".init.data")
#define INIT_BSS    SECTION(".init.bss")
#define PERCPU_DATA SECTION(".percpu.data")
#define PERCPU_BSS  SECTION(".percpu.bss")

// #ifndef offsetof
// #define offsetof(t,m) ((size_t)&((t*)0)->m)
// #endif

#ifndef containerof
#define containerof(p,t,m) ((t*)((size_t)(p) - __builtin_offsetof(t,m)))
#endif

// 等待时间
// tick=0 表示下一次时钟中断立即运行
#define NOWAIT  -1
#define FOREVER -2

#endif // WHEEL_H
