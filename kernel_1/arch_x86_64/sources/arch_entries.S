// 异常、中断、系统调用入口，以及任务切换

.global isr_entries
.global isr_dummy
.global switch_task

.extern g_int_depth
.extern g_int_rsp
.extern dispatch_int
.extern g_tid_curr
.extern g_tid_next


//------------------------------------------------------------------------------
// 使用宏定义一些代码片段
//------------------------------------------------------------------------------

// 开启 alternate macro 模式
// 可以使用 % 将符号内容转换成字符串
.altmacro

.macro def_sym vec
.global isr\vec
isr\vec:
.endm

.macro ref_sym vec
    .quad isr\vec
.endm

// 没有错误码的异常/中断入口
.macro isr_no_errcode vec, stub
    pushq   $-1
    pushq   %rdi
    movl    $\vec\(), %edi
    jmp     \stub
.endm

// 有错误码的异常/中断入口
.macro isr_with_errcode vec, stub
    pushq   %rdi
    movl    $\vec\(), %edi
    jmp     \stub
.endm

// 保存 Sys-V-ABI 规定的 scratch regs，以及 rbp
// 不含 rdi，中断入口处就已经保存了
.macro save_scratch_regs
    pushq   %rsi
    pushq   %rdx
    pushq   %rcx
    pushq   %rax
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %rbp
.endm

// 恢复 Sys-V-ABI 规定的 scratch regs，以及 rbp
// 恢复成刚刚发生中断时的栈状态（错误码还未出栈）
.macro restore_scratch_regs
    popq    %rbp
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rax
    popq    %rcx
    popq    %rdx
    popq    %rsi
    popq    %rdi
.endm


//------------------------------------------------------------------------------
// 中断、异常入口位置，也是 IDT 中填写的地址
//------------------------------------------------------------------------------

.section ".init.data", "aw"

.balign 8, 0
isr_entries:
.set i, 0
.rept 256
    ref_sym %i
    .set i, i + 1
.endr

.section ".text", "ax"
.code64

// 前 32 个是异常
.set i, 0
.rept 32
    .balign 8, 0x90
    def_sym %i
    .if ((10 <= i) && (i <= 14)) || (i == 17)
        isr_with_errcode i, exception_stub
    .else
        isr_no_errcode i, exception_stub
    .endif
    .set i, i + 1
.endr

// 后面的全都是中断
.set i, 32
.rept 256 - 32
    .balign 8, 0x90
    def_sym %i
    isr_no_errcode i, interrupt_stub
    .set i, i + 1
.endr

isr_dummy:
    iretq


//------------------------------------------------------------------------------
// 统一的异常、中断处理流程，从入口位置跳转而来，以及任务切换代码
//------------------------------------------------------------------------------

// 此时的栈结构（向下生长）：
// high |    SS    |
//      |   RSP    |
//      |  RFLAGS  |
//      |    CS    | <-- RSP + 0x18
//      |   RIP    |
//      | ERR_CODE |
//  low |   RDI    | <-- RSP


// 统一的异常处理流程，不允许重入，不切换栈，不切换任务
exception_stub:
    save_scratch_regs

    // 如果来自 ring3，则已经切换到 TSS.rsp0
    // 如果来自 ring0，则已经处于内核栈（任务栈或中断栈）
    pushq   $0
    pushq   $0
    movq    %rsp, %rbp
    call    dispatch_int
    addq    $16, %rsp

    restore_scratch_regs    // 从栈上恢复寄存器
    addq    $8, %rsp        // 错误码也要出栈
    iretq


// 统一的中断处理流程，允许重入，可以切换任务
interrupt_stub:
    save_scratch_regs

    // 增加中断深度，检查是否重入
    // 检查是否第一次进入中断，如果是，就切换到中断栈（防止任务栈溢出）
    incl    %gs:(g_int_depth)
    cmpl    $1, %gs:(g_int_depth)
    jne     nested_enter

    // 没有重入，需要切换到中断栈
    // 首先将 rsp 保存到当前 TCB
    movq    %gs:(g_tid_curr), %rax
    movq    %rsp, %rsi
    movq    %rsi, (%rax)
    movq    %gs:(g_int_rsp), %rsp

nested_enter:
    // 建立一个全新的调用栈
    pushq   $0
    pushq   $0
    movq    %rsp, %rbp

    // 重新打开中断，并调用中断处理函数
    // 第一个参数 rdi 代表向量号
    // 第二个参数 rsi 指向任务栈顶，即 int_context
    sti
    call    dispatch_int
    cli
    addq    $16, %rsp

    // 中断深度减一，检查是否即将退出最外层中断
    decl    %gs:(g_int_depth)
    cmpl    $0, %gs:(g_int_depth)
    jne     int_return

    // 是最外层中断，即将返回非中断状态
    // 换回（新）任务栈，可能是不同的任务
    movq    %gs:(g_tid_next), %rsi
    movq    (%rsi), %rsp

    // 本次中断可能执行了调度，检查是否需要切换任务
    movq    %gs:(g_tid_curr), %rdi
    cmpq    %rdi, %rsi
    je      int_return // 如果不切换任务，则直接返回

swap_context:
    // 如果需要切换任务，把剩下的寄存器保存到 TCB
    // 大部分寄存器已经在中断发生时保存在了任务栈上
    // 此时状态：
    //  - rdi 前一个 TCB
    //  - rsi 后一个 TCB
    //  - rsp 指向后一个任务的内核栈
    movq    %rbx, 0x08(%rdi)
    movq    %r12, 0x10(%rdi)
    movq    %r13, 0x18(%rdi)
    movq    %r14, 0x20(%rdi)
    movq    %r15, 0x28(%rdi)
    movq    %rsi, %gs:(g_tid_curr) // 更新 tid_curr
    movq    0x08(%rsi), %rbx
    movq    0x10(%rsi), %r12
    movq    0x18(%rsi), %r13
    movq    0x20(%rsi), %r14
    movq    0x28(%rsi), %r15

int_return:
    restore_scratch_regs    // 从栈上恢复寄存器
    addq    $8, %rsp        // 错误码也要出栈
    iretq


// 模拟发生了一次中断，触发任务切换，切换到 tid_next
switch_task:
    pushfq
    cli             // 关闭中断，进入临界区（防止 tid_next 变化）
    popq    %rax    // 获取 rflags 原值

    cmpl    $0, %gs:(g_int_depth)
    jne     1f  // 处于中断，直接返回（中断退出时会自动切换任务）
    movq    %gs:(g_tid_curr), %rdi
    movq    %gs:(g_tid_next), %rsi
    cmpq    %rdi, %rsi
    je      1f  // tid_curr==tid_next，不需要切换，直接返回

    popq    %rcx        // 获取返回地址，也是回到本任务时开始运行的位置
    movq    %rsp, %rdx  // 获取调用本函数前的 rsp
    andq    $~15, %rsp  // rsp 按 16 字节对齐
    pushq   $0x10       // ss
    pushq   %rdx        // rsp
    pushq   %rax        // rflags
    pushq   $0x08       // cs
    pushq   %rcx        // rip

    // 接下来 10 个字段：err,rdi,rsi,rdx,rcx,rax,r8,r9,r10,r11
    // 这些寄存器都是 caller-saved，也就是该函数可以随意使用
    // 所以不用真的保存它们，留出足够空间，假装保存了即可
    subq    $80, %rsp

    // 保存 rbp（仍为调用 switch_task 之前的值）
    pushq   %rbp

    movq    %rsp, 0(%rdi) // 将当前任务栈的位置保存在 TCB
    movq    0(%rsi), %rsp // 加载新任务的内核栈
    jmp     swap_context

1:
    pushq   %rax
    popfq
    ret
