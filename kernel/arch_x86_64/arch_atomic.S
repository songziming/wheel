// 原子操作，实现 arch_api.h 里面的接口

.global atomic32_get
.global atomic64_get
.global atomic_get

.global atomic32_get_add
.global atomic64_get_add
.global atomic_get_add

.text
.code64


atomic32_get:
    movl    (%rdi), %eax
    ret

atomic64_get:
atomic_get:
    movq    (%rdi), %rax
    ret

atomic32_get_add:
    movl        %esi, %eax
    lock xaddl  %eax, (%rdi)
    ret

atomic64_fetch_add:
atomic_fetch_add:
    movq        %rsi, %rax
    lock xaddq  %rax, (%rdi)
    ret
