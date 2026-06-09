.global syscall64
.global syscall80
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
    // TODO syscall exit task

.text
.code64

syscall64:
    syscall
    ret

syscall80:
    int $0x80
    ret
