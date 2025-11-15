#ifndef WHEEL_H
#define WHEEL_H

#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#define UNUSED      __attribute__((unused))
#define PURE        __attribute__((pure))
#define WEAK        __attribute__((weak))
#define PRINTF(s,a) __attribute__((format(printf,s,a)))
#define ALIGNED(x)  __attribute__((aligned(x)))

#if defined(UNIT_TEST)
    #define SECTION(x)
    #define ASM(...)
    #define ASMV(...)
#else
    #define SECTION(x)  __attribute__((section(x)))
    #define ASM         __asm__
    #define ASMV        __asm__ volatile
#endif

#define CONST       SECTION(".rodata")
#define INIT_TEXT   SECTION(".init.text")
#define INIT_DATA   SECTION(".init.data")

#endif // WHEEL_H