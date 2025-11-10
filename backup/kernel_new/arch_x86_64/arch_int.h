#ifndef ARCH_INT_H
#define ARCH_INT_H

#include <common.h>

// 中断栈上下文
typedef struct regs {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    // 上面的寄存器在中断时、系统调用时才会保存
    // 下面的寄存器在所有情况下都会保存
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rax;

    // 下面是中断异常发生时自动入栈的内容
    uint64_t errcode;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} regs_t;

INIT_TEXT void int_init();

void set_int_handler(int vec, void (*handler)(int, regs_t*));

#endif // ARCH_INT_H
