#include <linkage.h>

.section .text
.code64

GLOBAL_FUNC(load_gdtr)
GLOBAL_FUNC(load_idtr)
GLOBAL_FUNC(load_tr)

GLOBAL_FUNC(irq_entries)
GLOBAL_FUNC(exception_stub)
GLOBAL_FUNC(interrupt_stub)
GLOBAL_FUNC(syscall_entry)
GLOBAL_FUNC(task_entry)
GLOBAL_FUNC(task_switch)
GLOBAL_FUNC(user_return)

EXTERN_DATA(isr_tbl)
EXTERN_DATA(int_depth)
EXTERN_DATA(int_rsp)

EXTERN_DATA(tid_prev)
EXTERN_DATA(tid_next)
EXTERN_DATA(no_preempt)

EXTERN_FUNC(work_dequeue)

//------------------------------------------------------------------------------
// helper functions used by `cpu.c`

load_gdtr:
    lgdt    (%rdi)
    pushq   $8
    pushq   $_refresh
    lretq
_refresh:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    ret

load_idtr:
    lidt    (%rdi)
    ret

load_tr:
    ltr     %di
    ret

//------------------------------------------------------------------------------
// exception and interrupt entry points

// save all registers to stack (except %rax)
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

// restore registers from stack
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

#define no_errcode(vec, stub)   \
    pushq   $-1;                \
    pushq   %rax;               \
    movl    $vec, %eax;         \
    jmp     stub;

#define with_errcode(vec, stub) \
    pushq   %rax;               \
    movl    $vec, %eax;         \
    jmp     stub;               \
    pushq   $-1;

.balign 16
irq_entries:
    vector = 0
    .rept 256

    .if ((10 <= vector) && (vector <= 14)) || (vector == 17)
        with_errcode(vector, exception_stub)
    .elseif (vector < 32)
        no_errcode(vector, exception_stub)
    .else
        no_errcode(vector, interrupt_stub)
    .endif

    .balign 16
    vector = vector + 1
    .endr

// stack layout at exception_stub/interrupt_stub:
// High |    SS    |
//      |   RSP    |
//      |  RFLAGS  |
//      |    CS    | <-- %rsp + 0x18
//      |   RIP    |
//      | ERR CODE |
// Low  |   RAX    | <-- %rsp

// common exception handling logic
// no reentrancy, no task switching
exception_stub:
    testl   $3, 0x18(%rsp)          // check RPL field of selector CS
    jz      1f                      // skip swapgs if comming from kernel mode
    swapgs                          // swap gsbase if comming from user mode
1:
    save_regs_except_rax            // %rax already saved, save rest registers
    movl    %eax, %edi              // rdi = vector number, clear upper 32 bits
    movq    %rsp, %rsi              // rsi = stack frame
    movq    %rsp, %rbx              // save old stack frame in rbx

    pushq   $0                      // dummy return address
    pushq   $0                      // dummy rbp value
    movq    %rsp, %rbp              // point to current stack frame

    movq    $isr_tbl, %rax
    movq    (%rax, %rdi, 8), %rax   // rax = isr_tbl[rdi]
    call    * %rax
    movq    %rbx, %rsp              // restore old stack frame

    restore_all_regs                // restore all saved registers
    testl   $3, 0x10(%rsp)          // check RPL field of selector CS
    jz      2f                      // skip swapgs if returning to kernel mode
    swapgs                          // swap gsbase if returning to user mode
2:
    addq    $8, %rsp                // skip error code
    iretq

// common interrupt handling logic
// reentrant and supports task switching
interrupt_stub:
    testl   $3, 0x18(%rsp)          // check RPL field of selector CS
    jz      1f                      // skip swapgs if comming from kernel mode
    swapgs                          // swap gsbase if comming from user mode
1:
    save_regs_except_rax            // %rax already saved, save rest registers
    movl    %eax, %edi              // rdi = vector number, clear upper 32 bits
    movq    %rsp, %rsi              // rsi = stack frame
    movq    %rsp, %rbx              // save old stack frame in rbx

    incl    %gs:(int_depth)
    cmpl    $1, %gs:(int_depth)
    jne     2f                      // skip stack switching if re-entered

    movq    %gs:(tid_prev), %rax
    movq    %rbx, (%rax)            // tid_prev->regs.rsp = &int_frame
    movq    %gs:(int_rsp), %rsp     // switch to the interrupt stack
2:
    pushq   $0                      // dummy return address
    pushq   $0                      // dummy rbp value
    movq    %rsp, %rbp              // point to current stack frame

    movq    $isr_tbl, %rax
    movq    (%rax, %rdi, 8), %rax   // rax = isr_tbl[rdi]
    sti
    call    * %rax
    cli

    decl    %gs:(int_depth)
    cmpl    $0, %gs:(int_depth)     // if we're still inside ISR
    jne     3f                      // no need to switch stack and gsbase

    call    work_dequeue            // flush works (still inside int stack)
    movq    %rbx, %rsp              // restore old stack frame

    cmpl    $0, %gs:(no_preempt)    // if we've disabled preemption
    jne     4f                      // then no task switch is performed

    movq    %gs:(tid_next), %rdi    // destination task
    // movq    %gs:(tid_prev), %rsi    // source task
    // cmpq    %rdi, %rsi
    // je      4f                      // same task, no need to switch

load_tid_next:
    movq    %rdi, %gs:(tid_prev)    // update `tid_prev`
    movq    0x00(%rdi), %rsp
4:
    testl   $3, 0x88(%rsp)          // offset of rsp in int_frame
    jz      3f
    swapgs
3:
    restore_all_regs                // restore all saved registers
    addq    $8, %rsp                // skip error code
    iretq

//------------------------------------------------------------------------------
// system call entry

syscall_entry:
    swapgs
    //
    swapgs
    sysretq

//------------------------------------------------------------------------------
// entry of task

task_entry:
    pushq   $0                      // dummy return addr
    pushq   $0                      // dummy rbp value
    movq    %rsp, %rbp

    call    * %rax                  // start running task code
    call    task_exit               // remove this task
1:
    hlt
    jmp     1b

//------------------------------------------------------------------------------
// save tid_prev, load tid_next

task_switch:
    pushfq
    cli                             // disable interrupt for now
    popq    %rax                    // rflags before cli

    cmpq    $0, %gs:(int_depth)
    jne     1f                      // no switch if inside ISR
    cmpq    $0, %gs:(no_preempt)
    jne     1f                      // no switch if preemption is locked

    movq    %gs:(tid_prev), %rsi    // load `tid_prev` to rsi
    movq    %gs:(tid_next), %rdi    // load `tid_next` to rdi
    cmpq    %rdi, %rsi
    je      1f                      // same task, no need to switch

    movq    %cs, %r8                // r8  = cs
    popq    %r9                     // r9  = rip, restart from caller directly
    movq    %ss,  %r10              // r10 = ss
    movq    %rsp, %r11              // r11 = rsp
    pushq   %r10                    // ss
    pushq   %r11                    // rsp
    pushq   %rax                    // rflags
    pushq   %r8                     // cs
    pushq   %r9                     // rip, return address
    pushq   $0                      // error code
    pushq   $0                      // rax
    save_regs_except_rax            // rest of int_frame
    movq    %rsp, (%rsi)            // tid_prev->regs.rsp = &int_frame

    jmp     load_tid_next

1:
    pushq   %rax
    popfq
    ret

//------------------------------------------------------------------------------
// return to user mode

user_return:
    pushfq
    cli                             // disable interrupt for now
    popq    %rax                    // rflags before cli
    pushq   $0x23                   // ss
    pushq   %rsi                    // rsp
    pushq   %rax                    // rflags
    pushq   $0x2b                   // cs
    pushq   %rdi                    // rip
    swapgs
    iretq

