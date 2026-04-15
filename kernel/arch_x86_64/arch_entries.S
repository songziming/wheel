// CPU 中断异常入口、系统调用入口、任务切换入口

.global isr_entries
.global syscall_entry

.extern g_int_depth
.extern g_int_stack_top
.extern g_prev_task
.extern g_next_task
.extern irq_handlers

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

// rdi 已经在入口点保存，现在 rdi 保存向量号
#define push_caller_saved_regs  \
    pushq   %rsi;               \
    pushq   %rdx;               \
    pushq   %rcx;               \
    pushq   %r8;                \
    pushq   %r9;                \
    pushq   %r10;               \
    pushq   %r11;               \
    pushq   %rax
#define pop_caller_saved_regs   \
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
#define push_callee_saved_regs  \
    pushq   %rbx;               \
    pushq   %rbp;               \
    pushq   %r12;               \
    pushq   %r13;               \
    pushq   %r14;               \
    pushq   %r15
#define pop_callee_saved_regs   \
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

    push_caller_saved_regs
    subq    $48, %rsp       // 跳过 6 个 callee-saved regs
    movq    %rsp, %rsi      // 参数：当前栈顶/寄存器文件

    // 调用 C 函数，函数会自己保存 callee-saved regs
    movq    irq_handlers(,%rdi,8), %rax
    call    *%rax

    addq    $48, %rsp       // 跳过 6 个 callee-save regs
    pop_caller_saved_regs
    addq    $8, %rsp        // 跳过错误码

    testl   $3, 8(%rsp)     // 检查 CS.RPL
    jz      exp_to_ring0    // 返回到内核态
    swapgs                  // 返回到用户态
exp_to_ring0:
    iretq

// 中断响应流程，需要切换中断栈，允许重入，可能切换任务
// 需要保存全部寄存器（不含浮点寄存器）
interrupt_stub:
    testl   $3, 0x18(%rsp)  // 检查 CS.CPL
    jz      int_from_ring0  // 如果 CS.CPL=0，说明来自 ring0
    swapgs                  // 如果来自用户态，需要交换 gsbase
int_from_ring0:

    push_caller_saved_regs
    push_callee_saved_regs
    movq    %rsp, %rsi      // 参数：当前栈顶/寄存器文件

    // 所有寄存器已经保存在 regs_t（位于任务内核栈），检查是否需要切换中断栈
    incl    %gs:(g_int_depth)
    cmpl    $1, %gs:(g_int_depth)
    jne     int_enter_nested

    // 不是重入，需要切换到中断栈，把当前栈位置也记录下来
    movq    %gs:(g_prev_task), %rax
    movq    %rsi, (%rax) // THISCPU(g_prev_task)->regs = %rsp
    // movq    %rsp, %gs:(g_curr_stack)
    movq    %gs:(g_int_stack_top), %rsp
int_enter_nested:

    // 调用 C 函数处理中断
    movq    irq_handlers(,%rdi,8), %rax
    call    *%rax

    // 判断是否退出最后一层中断，如果是，需要执行任务切换
    decl    %gs:(g_int_depth)
    cmpl    $0, %gs:(g_int_depth)
    jne     int_return_nested

    // 即将推出最后一层中断，在这里执行异步任务
    // TODO work_q_flush

    // 切换到原来的栈顶
    // TODO 任务切换也在这里实现，替换成新任务的栈顶地址
    movq    %gs:(g_next_task), %rax
    movq    (%rax), %rsp // rsp = THISCPU(g_next_task)->regs
    // movq    %gs:(g_curr_stack), %rsp
int_return_nested:

    pop_callee_saved_regs
    pop_caller_saved_regs
    addq    $8, %rsp        // 跳过错误码

    testl   $3, 8(%rsp)     // 检查 CS.RPL
    jz      int_to_ring0    // 返回到内核态
    swapgs                  // 返回到用户态
int_to_ring0:
    iretq


// 系统调用入口，类似
// 使用 syscall 命令跳转
syscall_entry:
    sysret
