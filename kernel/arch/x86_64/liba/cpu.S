#include <linkage.h>

.section .text
.code64

GLOBAL_FUNC(load_gdtr)
GLOBAL_FUNC(load_idtr)
GLOBAL_FUNC(load_tr)

GLOBAL_FUNC(int_0_entry)
GLOBAL_FUNC(int_1_entry)
GLOBAL_FUNC(exception_stub)
GLOBAL_FUNC(interrupt_stub)
GLOBAL_FUNC(syscall_entry)

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
.macro save_regs
    pushq   %rbx
    pushq   %rcx
    pushq   %rdx
    pushq   %rdi
    pushq   %rsi
    pushq   %rbp
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
.endm

// restore registers from stack
.macro restore_regs
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rbp
    popq    %rsi
    popq    %rdi
    popq    %rdx
    popq    %rcx
    popq    %rbx
    popq    %rax
.endm

.balign 16
int_0_entry:
    pushq   $-1
    pushq   %rax
    movl    $0, %eax            // clear upper 32 bits
    jmp     exception_stub

.balign 16
int_1_entry:
    pushq   $-1
    pushq   %rax
    movl    $1, %eax            // clear upper 32 bits
    jmp     exception_stub

vec = 2
.rept 256-2
.balign 16

// generate ISR entry for every interrupt/exception
.if ((10 <= vec) && (vec <= 14)) || (vec == 17)
    // exception with error code
    pushq   %rax
    movl    $ vec, %eax         // clear upper 32 bits
    jmp     exception_stub
    pushq   $-1                 // keep stub length same with no-err code ones
.elseif (vec < 32)
    // exception without error code
    pushq   $-1
    pushq   %rax
    movl    $ vec, %eax         // clear upper 32 bits
    jmp     exception_stub
.else
    // interrupt (no error code)
    pushq   $-1
    pushq   %rax
    movl    $ vec, %eax
    jmp     interrupt_stub
.endif

vec = vec + 1
.endr

exception_stub:
    iretq

interrupt_stub:
    iretq

syscall_entry:
    sysretq
