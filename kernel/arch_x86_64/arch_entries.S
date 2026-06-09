// CPU 中断异常入口、系统调用入口、任务切换入口

.global isr_entries
.global syscall_entry
.global arch_task_switch
.global arch_enter_ring3

.extern g_int_depth
.extern g_int_stack_top
.extern g_preempt_depth
.extern g_tss       // from gdt_idt_tss.c
.extern g_tid_prev
.extern g_tid_next
.extern irq_handlers
.extern work_flush
.extern do_syscall

.altmacro // 需要在 macro 和循环里使用变量


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
// 中断异常入口
//------------------------------------------------------------------------------

.text
.code64

.macro isr_no_err vec, stub
isr_\vec:
    pushq   $-1
    pushq   %rdi
    movl    $\vec, %edi
    jmp     \stub
.endm

.macro isr_with_err vec, stub
isr_\vec:
    pushq   %rdi
    movl    $\vec, %edi
    jmp     \stub
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


//------------------------------------------------------------------------------
// 中断和异常的统一处理后端
//------------------------------------------------------------------------------

// 发生中断时的栈结构，向低地址生长，最后的 RDI 是我们压栈的
// High |    SS    |
//      |   RSP    |
//      |  RFLAGS  |
//      |    CS    | <-- %rsp + 0x18
//      |   RIP    |
//      | ERR CODE |
// Low  |   RDI    | <-- %rsp

// rdi 已经在入口点保存，现在 rdi 表示向量号
#define push_8_caller_saved_regs  \
    pushq   %rsi;               \
    pushq   %rdx;               \
    pushq   %rcx;               \
    pushq   %r8;                \
    pushq   %r9;                \
    pushq   %r10;               \
    pushq   %r11;               \
    pushq   %rax
#define pop_9_caller_saved_regs   \
    popq    %rax;               \
    popq    %r11;               \
    popq    %r10;               \
    popq    %r9;                \
    popq    %r8;                \
    popq    %rcx;               \
    popq    %rdx;               \
    popq    %rsi;               \
    popq    %rdi

// 只要不切换任务，这些寄存器就无需保存
#define push_6_callee_saved_regs  \
    pushq   %rbx;               \
    pushq   %rbp;               \
    pushq   %r12;               \
    pushq   %r13;               \
    pushq   %r14;               \
    pushq   %r15
#define pop_6_callee_saved_regs \
    popq    %r15;               \
    popq    %r14;               \
    popq    %r13;               \
    popq    %r12;               \
    popq    %rbp;               \
    popq    %rbx

// 异常响应流程，不切换中断栈，不允许重入，不切换任务
// 异常更像是被动的函数调用，无需保存 callee-save regs，因为调用 C 函数时会自动保存
exception_stub:
    testl   $3, 0x18(%rsp)  // 检查 CS.CPL
    jz      exp_from_ring0  // 如果 CS.CPL=0，说明来自 ring0
    swapgs                  // 如果来自用户态，需要交换 gsbase
exp_from_ring0:

    push_8_caller_saved_regs // 这些函数会被 ISR 破坏，需要保存
    subq    $48, %rsp       // 跳过 6 个 callee-saved regs，ISR 自己会保存
    movq    %rsp, %rsi      // 参数：当前栈顶/寄存器文件

    // 建立一个新的函数栈，连接到发生异常的地方
    // 就像是从异常位置调用了本函数
    push    0x80(%rsi)  // frame+160, RIP
    push    %rbp
    movq    %rsp, %rbp

    movq    irq_handlers(,%rdi,8), %rax
    call    *%rax
    addq    $16, %rsp   // 跳过 dummy-stack-frame

    addq    $48, %rsp       // 跳过 6 个 callee-save regs
    pop_9_caller_saved_regs
    addq    $8, %rsp        // 跳过错误码

    testl   $3, 8(%rsp)     // 检查 CS.RPL
    jz      exp_to_ring0    // 返回到内核态
    swapgs                  // 返回到用户态
exp_to_ring0:
    iretq

// 中断响应流程，需要切换中断栈，可能切换任务
// 需要保存全部寄存器（不含浮点寄存器）
// 中断内部可能异常，但我们不允许中断重入（为了安全，还是检查一下）
interrupt_stub:
    testl   $3, 0x18(%rsp)  // 检查 CS.CPL
    jz      int_from_ring0  // 如果 CS.CPL=0，说明来自 ring0
    swapgs                  // 如果来自用户态，需要交换 gsbase
int_from_ring0:

    push_8_caller_saved_regs
    push_6_callee_saved_regs
    movq    %rsp, %rsi      // 参数：当前栈顶/寄存器文件

    // 所有寄存器已经保存在 regs_t（位于任务内核栈），检查是否需要切换中断栈
    incl    %gs:(g_int_depth)
    cmpl    $1, %gs:(g_int_depth)
    jne     int_enter_nested

    // 不是重入，当前处于任务栈
    // 切换到中断栈，把当前栈顶保存在 TCB
    movq    %gs:(g_tid_prev), %rax
    movq    %rsi, (%rax) // g_tid_prev->stack_top = %rsp
    movq    %gs:(g_int_stack_top), %rsp

int_enter_nested:
    // 需要伪造一个 stack-frame，防止 stack trace 越界
    pushq   $0
    pushq   %rbp
    movq    %rsp, %rbp

    // 调用 C 函数处理中断
    movq    irq_handlers(,%rdi,8), %rax
    call    *%rax
    addq    $16, %rsp

    // 判断是否退出最后一层中断，如果是，需要执行任务切换
    cmpl    $1, %gs:(g_int_depth)
    jne     iret_nested

    // 即将退出最后一层中断，在这里执行异步任务
    pushq   $0
    pushq   %rbp
    movq    %rsp, %rbp
    call    work_flush
    addq    $16, %rsp

    // 如果禁用抢占被禁用，跳过任务切换
    cmpl    $0, %gs:(g_preempt_depth)
    cmovne  %gs:(g_tid_prev), %rdi
    jne     iret_to_task

    // 切换到新任务的栈顶（也可能是同一个任务）
    movq    %gs:(g_tid_next), %rdi
iret_to_task:
    movq    8(%rdi), %rax           // rax = g_tid_next->stack0
    movq    %rax, %gs:(g_tss+4)     // tss->rsp0 = rax
    movq    (%rdi), %rsp            // rsp = g_tid_next->stack_top
    movq    %rdi, %gs:(g_tid_prev)  // prev_task = next_task
iret_nested:
    decl    %gs:(g_int_depth)
    pop_6_callee_saved_regs
    pop_9_caller_saved_regs
    addq    $8, %rsp        // 跳过错误码

    testl   $3, 8(%rsp)     // 检查 CS.RPL
    jz      iret_to_ring0   // 返回到内核态
    swapgs                  // 返回到用户态
iret_to_ring0:
    iretq


// 不执行调度函数，不经过中断栈，直接从当前任务切换到下一个任务
// 保存当前任务的上下文，然后执行中断返回流程
// 当这个函数返回，说明又重新回到了原本的任务
arch_task_switch:
    pushfq
    cli                     // 关中断，防止 tid_prev/tid_next 改变

    cmpl    $0, %gs:(g_int_depth)   // 如果已处于中断
    jne     noswitch                // 则不必切换任务（中断退出时会自动切换）
    cmpl    $0, %gs:(g_preempt_depth)
    jne     noswitch

    movq    %gs:(g_tid_prev), %rsi
    movq    %gs:(g_tid_next), %rdi
    cmpq    %rsi, %rdi
    je      noswitch        // 目标任务相同，不用切换

    popq    %rax            // 获取 rflags
    movq    %rsp, %r8       // 获取 rsp
    andq    $-16, %rsp      // 栈帧按 16 字节对齐

    // 模仿发生了一次中断
    pushq   $0x10           // ss
    pushq   %r8             // rsp
    pushq   %rax            // rflags
    pushq   $0x08           // cs
    pushq   $resume         // rip 从这里恢复
    subq    $80, %rsp       // errcode, rdi, and other 8 caller-saved regs
    push_6_callee_saved_regs
    movq    %rsp, (%rsi)    // tid_prev->regs = rsp

    incl    %gs:(g_int_depth)
    jmp     iret_to_task

noswitch:
    popfq
resume:
    ret


// 系统调用入口，使用 syscall 跳转而来，此时中断关闭，处于用户栈
// rip 保存在 rcx，rflags 保存在 r11（这两个都是 scratch-regs，需要保存）
syscall_entry:
    pushq   %rcx
    pushq   %r11
    swapgs
    movq    %gs:(g_tid_prev), %r11
    movq    %rsp, 16(%r11)  // save user stack
    movq    8(%r11), %rsp   // switch to kernel stack

    // 执行系统调用，中间可能切换任务
    sti
    call    do_syscall
    cli

    // 回到用户栈，返回用户态
    movq    %gs:(g_tid_prev), %r11
    movq    16(%r11), %rsp
    swapgs
    popq    %r11
    popq    %rcx
    sysretq


// 主动跳转到 ring3，用于新进程启动
// rdi 表示目标用户态代码入口地址
// rsi 表示用户栈的栈顶地址
arch_enter_ring3:
    // 也可以模拟中断返回（iretq）
    // 但我们使用 sysret，自己切换到用户栈，建立新的 frame
    pushfq
    cli                 // 关中断，防止swapgs之后中断
    popq    %r11        // r11 = ring3-rflags
    movq    %rdi, %rcx  // rcx = ring3-rip
    movq    %rsi, %rsp
    pushq   $0          // dummy return addr
    swapgs
    sysretq
