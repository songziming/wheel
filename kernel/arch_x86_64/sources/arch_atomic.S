// 原子操作

.global atomic32_get
.global atomic32_inc
.global thiscpu_atomic32_get
.global thiscpu_atomic32_inc

.text
.code64

atomic32_get:
    movl        (%rdi), %eax
    ret

atomic32_inc:
    movl        $1, %eax
    lock xaddl  %eax, (%rdi)
    ret

thiscpu_atomic32_get:
    movl        %gs:(%rdi), %eax
    ret

thiscpu_atomic32_inc:
    movl        $1, %eax
    lock xaddl  %eax, %gs:(%rdi)
    ret
