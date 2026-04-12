#ifndef ARCH_X86_64_ARCH_INT_H
#define ARCH_X86_64_ARCH_INT_H

#include <wheel.h>

// 中断栈上下文
// 需要和 arch_entries.S 里面寄存器压栈、出栈的顺序匹配
typedef struct regs {
    // 这些寄存器在中断&系统调用时才会保存（需要切换任务时）
    // 这些属于 callee-saved-regs，子函数需要恢复调用前的取值
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;

    // 下面的寄存器在所有情况下都会保存
    // 这些属于 caller-saved-regs，子函数可以破坏
    uint64_t rax;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;

    // 下面是中断异常发生时自动入栈的内容
    uint64_t errcode;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} regs_t;

INIT_TEXT void int_init();

#endif // ARCH_X86_64_ARCH_INT_H
