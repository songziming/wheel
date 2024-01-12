// 中断异常入口、系统调用入口

.global isr_entries
.global syscall_entry
.global arch_task_switch

// arch_int.c
.extern g_handlers
.extern g_int_depth
.extern g_int_stack

// sched.c
.extern g_tid_prev
.extern g_tid_next


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
    popq    %r15;       \
    popq    %r14;       \
    popq    %r13;       \
    popq    %r12;       \
    popq    %rbp;       \
    popq    %rbx;       \
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


//------------------------------------------------------------------------------
// 异常处理流程，不允许重入，不切换中断栈，不切换任务
//------------------------------------------------------------------------------

// 异常更像是被动的函数调用，只要将 ABI 规定的 caller-saved regs 保存即可
exception_stub:
    testl   $3, 0x18(%rsp)  // 检查 CPL
    jz      exp_from_kernel // 如果 CPL=0，说明来自 ring0
    swapgs                  // 如果来自用户态，需要交换 gsbase
exp_from_kernel:
    save_exp_regs           // 保存寄存器
    subq    $48, %rsp       // 跳过 6 个 callee-save regs

    // 函数参数
    movq    %rax, %rdi      // 向量号
    movq    %rsp, %rsi      // 当前栈帧

    // 调用 C 函数处理异常
    movq    $g_handlers, %rax
    movq    (%rax, %rdi, 8), %rax
    call    *%rax

    addq    $48, %rsp       // 跳过 6 个 callee-save regs
    load_exp_regs           // 恢复寄存器
    addq    $8, %rsp        // 跳过错误码

    testl   $3, 8(%rsp)     // 检查 RPL
    jz      exp_to_kernel   // 返回到内核态
    swapgs                  // 返回到用户态
exp_to_kernel:
    iretq


//------------------------------------------------------------------------------
// 中断处理流程，允许重入，切换中断栈，可以切换任务
//------------------------------------------------------------------------------

interrupt_stub:
    testl   $3, 0x18(%rsp)  // 检查栈上 CS 最后选择子的特权级，判断是否来自 ring3
    jz      int_from_kernel // 如果 CPL=0，说明来自 ring0
    swapgs                  // 如果来自用户态，需要交换 gsbase
int_from_kernel:
    save_int_regs

    // 准备函数参数
    movq    %rax, %rdi      // 向量号
    movq    %rsp, %rsi      // 当前栈帧

    incl    %gs:(g_int_depth)
    cmpl    $1, %gs:(g_int_depth)
    jne     int_nested_enter

    // 只有最外层中断才需要切换到中断栈
    movq    %gs:(g_tid_prev), %rax
    movq    %rsp, (%rax)            // g_tid_prev->arch.regs = %rsp
    movq    %gs:(g_int_stack), %rsp

int_nested_enter:
    // 调用 C 函数
    movq    $g_handlers, %rax
    movq    (%rax, %rdi, 8), %rax
    call    *%rax

    // 判断是否退出最后一层中断，如果是，需要执行任务切换
    decl    %gs:(g_int_depth)
    cmpl    $0, %gs:(g_int_depth)
    jne     int_nested_return

    // 切换到新任务的栈顶
    movq    %gs:(g_tid_next), %rdi
load_tid_next:
    movq    %rdi, %gs:(g_tid_prev)
    movq    (%rdi), %rsp

int_nested_return:
    load_int_regs
    addq    $8, %rsp        // 跳过错误码

    testl   $3, 8(%rsp)     // 检查返回地址的 RPL，判断是否即将返回到 ring3
    jz      int_to_kernel   // 返回到内核态
    swapgs                  // 返回到用户态，需要交换 gsbase
int_to_kernel:
    iretq



//------------------------------------------------------------------------------
// 主动任务切换，来自内核态，正在使用内核栈
//------------------------------------------------------------------------------

// 模拟发生一次中断，切换到 g_tid_next
arch_task_switch:
    pushfq
    cli
    popq    %rax

    cmpq    $0, %gs:(g_int_depth)
    jne     no_yield    // 处于中断上下文，不许切换任务（中断退出时会自动切换）

    movq    %gs:(g_tid_prev), %rsi
    movq    %gs:(g_tid_next), %rdi
    cmpq    %rdi, %rsi
    je      no_yield    // 目标任务相同，不用切换

    // 模拟中断
    movq    %rsp, %r8
    andq    $-16, %rsp      // 栈帧按 16 字节对齐
    pushq   $0x10           // ss
    pushq   %r8             // rsp
    pushq   %rax            // rflags
    pushq   $0x08           // cs
    pushq   $yield_resume   // rip
    pushq   $0              // 错误码
    pushq   $0              // rax
    save_int_regs
    movq    %rsp, (%rsi)    // g_tid_prev->arch.regs = %rsp

    jmp     load_tid_next

no_yield:
    pushq   %rax
    popfq
yield_resume:
    ret



//------------------------------------------------------------------------------
// 系统调用入口，来自用户态
//------------------------------------------------------------------------------

syscall_entry:
    ret
