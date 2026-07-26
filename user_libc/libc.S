.global _syscall
.global _start
.extern main

.section ".head","ax"
.code64

// 用户态进程开始执行的位置，由 kernel-task 启动
_start:
    pushq   $0
    pushq   $0
    movq    %rsp, %rbp
    call    main

    movq    %rax, %rdi  // rdi = ret code
    xorq    %rax, %rax  // rax = 0 (sys_exit)
    syscall

// .text
// .code64

// _syscall:
//     syscall
//     ret
