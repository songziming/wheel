// 原子操作

.global atomic32_get
.global atomic32_inc

.text
.code64

atomic32_get:
    movl        (%rdi), %eax
    ret

atomic32_inc:
    movl        $1, %eax
    lock xaddl  %eax, (%rdi)
    ret
