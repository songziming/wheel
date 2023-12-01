// 中断异常入口、系统调用入口

// 发生中断时的栈结构，向低地址生长，最后的 RAX 是我们压栈的
// High |    SS    |
//      |   RSP    |
//      |  RFLAGS  |
//      |    CS    | <-- %rsp + 0x18
//      |   RIP    |
//      | ERR CODE |
// Low  |   RAX    | <-- %rsp

.global isr_entries



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

// 所有寄存器入栈（除了 RAX）
#define save_regs_except_rax    \
    pushq   %rbx;               \
    pushq   %rcx;               \
    pushq   %rdx;               \
    pushq   %rdi;               \
    pushq   %rsi;               \
    pushq   %rbp;               \
    pushq   %r8;                \
    pushq   %r9;                \
    pushq   %r10;               \
    pushq   %r11;               \
    pushq   %r12;               \
    pushq   %r13;               \
    pushq   %r14;               \
    pushq   %r15;

// 所有寄存器出栈（包括 RAX）
#define restore_all_regs        \
    popq    %r15;               \
    popq    %r14;               \
    popq    %r13;               \
    popq    %r12;               \
    popq    %r11;               \
    popq    %r10;               \
    popq    %r9;                \
    popq    %r8;                \
    popq    %rbp;               \
    popq    %rsi;               \
    popq    %rdi;               \
    popq    %rdx;               \
    popq    %rcx;               \
    popq    %rbx;               \
    popq    %rax;

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




// 所有异常的通用处理流程
exception_stub:
    iretq

// 所有中断的通用处理流程
interrupt_stub:
    iretq
