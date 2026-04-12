// CPU 中断异常入口、系统调用入口、任务切换入口

.global isr_entries
.extern handle_irq

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

// 保存异常寄存器（caller-save 寄存器无需保存, rdi 已经保存）
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

// 保存中断寄存器（全部都要保存）
#define push_callee_saved_regs  \
    pushq   %rbx;               \
    pushq   %rbp;               \
    pushq   %r12;               \
    pushq   %r13;               \
    pushq   %r14;               \
    pushq   %r15

#define pop_callee_saved_regs   \
    popq    %r15;       \
    popq    %r14;       \
    popq    %r13;       \
    popq    %r12;       \
    popq    %rbp;       \
    popq    %rbx;

// 发生中断时的栈结构，向低地址生长，最后的 RDI 是我们压栈的
// High |    SS    |
//      |   RSP    |
//      |  RFLAGS  |
//      |    CS    | <-- %rsp + 0x18
//      |   RIP    |
//      | ERR CODE |
// Low  |   RDI    | <-- %rsp

// 中断异常处理过程会使用大量栈空间，可能栈溢出，引发 double fault
// 应该使用 IST 机制，处理 double fault 使用专用栈
// 这可以保证处理 double fault 不会溢出，不会引发 triple fault

// 异常响应流程，不切换中断栈，不允许重入，不切换任务
// 异常更像是被动的函数调用，无需保存 callee-save regs，因为调用 C 函数时会自动保存
exception_stub:
    push_caller_saved_regs       // 保存寄存器，不含 callee-saved regs
    subq    $48, %rsp   // 跳过 6 个 callee-saved regs
    // movq    %rdi, %rdi  // 参数：向量号
    movq    %rsp, %rsi  // 参数：寄存器文件
    call    handle_irq  // 调用 C 函数，函数会自己保存 callee-saved regs
    addq    $48, %rsp   // 跳过 6 个 callee-save regs
    pop_caller_saved_regs       // 恢复寄存器
    addq    $8, %rsp    // 跳过错误码
    iretq

// 中断响应流程，需要切换中断栈，允许重入，可能切换任务
// 需要保存全部寄存器（不含浮点寄存器）
interrupt_stub:
    push_caller_saved_regs
    push_callee_saved_regs
    // movq    %rdi, %rdi  // 参数：向量号
    movq    %rsp, %rsi  // 参数：当前栈顶
    call    handle_irq  // 调用 C 函数处理中断
    pop_callee_saved_regs       // 恢复所有寄存器
    pop_caller_saved_regs
    addq    $8, %rsp    // 跳过错误码
    iretq
