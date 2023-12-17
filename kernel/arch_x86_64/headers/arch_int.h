#ifndef ARCH_INT_H
#define ARCH_INT_H

#include <def.h>

typedef struct exp_frame {
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rax;
    uint64_t errcode;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} PACKED exp_frame_t;

typedef struct int_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t errcode;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} PACKED int_frame_t;

// 如果异常和中断使用不同的栈结构，就只能定义两套函数指针，使用两套 setter
// TODO 或者让栈结构通用，只是异常不会填充完整的 frame
typedef void (*exp_handler_t)(int vec, exp_frame_t *f);
typedef void (*int_handler_t)(int vec, int_frame_t *f);

INIT_TEXT void int_init();

void set_exp_handler(int vec, exp_handler_t handler);
void set_int_handler(int vec, int_handler_t handler);

#endif // ARCH_INT_H
