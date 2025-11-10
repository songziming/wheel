// 原子操作

.global atomic8_add
.global atomic16_add

.global atomic32_get
.global atomic32_set
.global atomic32_add
.global atomic32_cas

.global atomic64_get
.global atomic64_set
.global atomic64_add
.global atomic64_cas

.global atomic_get
.global atomic_set
.global atomic_add
.global atomic_cas

.text
.code64

atomic8_add:
    movl        %esi, %eax
    lock xaddb  %al, (%rdi)
    ret

atomic16_add:
    movl        %esi, %eax
    lock xaddw  %ax, (%rdi)
    ret

atomic32_get:
    movl        (%rdi), %eax
    ret

atomic32_set:
    movl        %esi, %eax
    lock xchgl  %eax, (%rdi)
    ret

atomic32_add:
    movl        %esi, %eax
    lock xaddl  %eax, (%rdi)
    ret

atomic32_cas:
    movl        %esi, %eax
    lock
    cmpxchgl    %edx, (%rdi)
    ret

atomic64_get:
atomic_get:
    movq        (%rdi), %rax
    ret

atomic64_set:
atomic_set:
    movq        %rsi, %rax
    lock xchgq  %rax, (%rdi)
    ret

atomic64_add:
atomic_add:
    movq        %rsi, %rax
    lock xaddq  %rax, (%rdi)
    ret

atomic64_cas:
atomic_cas:
    movq        %rsi, %rax
    lock
    cmpxchgq    %rdx, (%rdi)
    ret
