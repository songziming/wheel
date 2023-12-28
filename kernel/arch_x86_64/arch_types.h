#ifndef ARCH_X86_64_TYPES_H
#define ARCH_X86_64_TYPES_H

// arch 公开数据类型定义

#include <stdint.h>

// 发生中断、异常、系统调用时，将寄存器按下面的格式保存在栈上
typedef struct arch_regs {
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
} PACKED arch_regs_t;

// 硬件相关的任务控制块
typedef struct arch_tcb {
    arch_regs_t *regs;  // 上一次中断保存的寄存器
    uint64_t     rsp0;  // 内核栈栈顶，运行 ring3 代码时需要填入 tss
} PACKED arch_tcb_t;

#endif // ARCH_X86_64_TYPES_H
