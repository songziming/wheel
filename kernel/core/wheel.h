#ifndef WHEEL_H
#define WHEEL_H

#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>

#define WEAK        __attribute__((weak))

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

#endif // WHEEL_H