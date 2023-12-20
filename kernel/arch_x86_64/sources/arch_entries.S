// 中断异常入口、系统调用入口

.global isr_entries
.global syscall_entry

// arch_int.c
.extern g_handlers
.extern g_int_depth


// 必须开启这个模式
.altmacro

//------------------------------------------------------------------------------
// 中断异常入口地址数组，用于填充 IDT
//------------------------------------------------------------------------------

.section ".init.data", "aw"

.macro isr_label vec
    .quad isr_\vec
.endm

isr_entries:
.set vector, 0
.rept 256
    isr_label %vector
    .set vector, vector + 1
.endr


//------------------------------------------------------------------------------
// 中断异常入口代码
//------------------------------------------------------------------------------

.text
.code64

// 保存异常寄存器（caller-save 寄存器无需保存, rax 已经保存）
#define save_exp_regs   \
    pushq   %rdi;       \
    pushq   %rsi;       \
    pushq   %rdx;       \
    pushq   %rcx;       \
    pushq   %r8;        \
    pushq   %r9;        \
    pushq   %r10;       \
    pushq   %r11

// 保存中断寄存器（全部都要保存）
#define save_int_regs   \
    save_exp_regs;      \
    pushq   %rbx;       \
    pushq   %rbp;       \
    pushq   %r12;       \
    pushq   %r13;       \
    pushq   %r14;       \
    pushq   %r15

#define load_exp_regs   \
    popq    %r11;       \
    popq    %r10;       \
    popq    %r9;        \
    popq    %r8;        \
    popq    %rcx;       \
    popq    %rdx;       \
    popq    %rsi;       \
    popq    %rdi;       \
    popq    %rax

#define load_int_regs   \
    popq    %r15;   \
    popq    %r14;   \
    popq    %r13;   \
    popq    %r12;   \
    popq    %rbp;   \
    popq    %rbx;   \
    load_exp_regs


// 没有异常码的异常入口，以及中断入口
.macro isr_no_err vec, stub
isr_\vec:
    pushq   $-1
    pushq   %rax
    movl    $\vec, %eax
    jmp     \stub
.endm

// 含有异常码的异常入口
.macro isr_with_err vec, stub
isr_\vec:
    pushq   %rax
    movl    $\vec, %eax
    jmp     \stub
    pushq   $-1
.endm




// 生成所有异常和中断的入口
.set vector, 0
.rept 256
    .balign 16
    .if ((10 <= vector) && (vector <= 14)) || (vector == 17)
        isr_with_err %vector, exception_stub
    .elseif (vector < 32)
        isr_no_err %vector, exception_stub
    .else
        isr_no_err %vector, interrupt_stub
    .endif
    .set vector, vector + 1
.endr




// 发生中断时的栈结构，向低地址生长，最后的 RAX 是我们压栈的
// High |    SS    |
//      |   RSP    |
//      |  RFLAGS  |
//      |    CS    | <-- %rsp + 0x18
//      |   RIP    |
//      | ERR CODE |
// Low  |   RAX    | <-- %rsp


// 中断/异常处理过程会占用大量栈空间，特别是保存寄存器，可能溢出
// 如果寄存器压栈过程溢出，就会导致 double-fault
// 有什么办法避免中断/异常时栈溢出？
//  - 如果来自 ring3，则自动切换到 tss->rsp3，一般将 tss->rsp3 设为 TCB 结构体
//  - 如果设置了 ist，则自动切换到 tss->ist[i]，每个 cpu 最多可以设置七个 ist
//      ist 机制常用于 NMI、double-fault、machine-check，其他的异常如果溢出，还能在 double-fault 中处理
//      如果 double-fault 内部又溢出，三重异常，机器就只能重启了
//      带有 ist 的异常处理流程中，要避免重入相同的异常，这会导致栈被覆盖
//  - 剩下的情况，来自内核代码。只能保证栈空间足够，发生异常也不会溢出。
//      内核代码流程可控



// 异常处理流程，不允许重入，不切换中断栈，不切换任务
// TODO 异常更像是被动的函数调用，无需备份所有寄存器，只要将 abi 规定的 caller-saved regs 保存即可
//      包括：rdi, rsi, rdx, rcx, r8, r9, rax, r10, r11
exception_stub:
    save_exp_regs   // 保存寄存器

    // TODO 所有寄存器已经入栈，应该切换到专门的异常栈

    // 函数参数
    movq    %rax, %rdi  // 向量号
    movq    %rsp, %rsi  // 当前栈帧

    // // 需要建立一个新的栈帧
    // pushq   80(%rsi)   // 返回地址（即发生异常的 rip）
    // pushq   %rbp // 外层栈帧的 rbp
    // movq    %rsp, %rbp // rbp 指向现在的栈帧

    // 调用 C 函数处理异常
    movq    $g_handlers, %rax
    movq    (%rax, %rdi, 8), %rax
    call    *%rax

    // 恢复栈，撤销当前栈帧
    movq    %rbp, %rsp
    addq    $16, %rsp

    load_exp_regs   // 恢复寄存器
    addq    $8, %rsp    // 跳过错误码
    iretq


// 中断处理流程，允许重入，切换中断栈，可以切换任务
interrupt_stub:
    // TODO 判断是否来自 ring3，决定是否执行 swapgs
    save_int_regs

    incl    %gs:(g_int_depth)
    // TODO 判断是否重入，决定是否切换中断栈

    // 函数参数
    movq    %rax, %rdi  // 向量号
    movq    %rsp, %rsi  // 当前栈帧

    // 调用 C 函数
    movq    $g_handlers, %rax
    movq    (%rax, %rdi, 8), %rax
    call    *%rax

    decl    %gs:(g_int_depth)
    // TODO 判断是否退出最后一层中断，决定是否切换到原式栈
    // TODO 判断是否即将回到 ring3，决定是否执行 swapgs

    load_int_regs
    addq    $8, %rsp    // 跳过错误码
    iretq


syscall_entry:
    ret
