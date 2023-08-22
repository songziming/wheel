#include <linkage.h>

GLOBAL_FUNC(atomic32_get)
GLOBAL_FUNC(atomic32_set)
GLOBAL_FUNC(atomic32_inc)
GLOBAL_FUNC(atomic32_dec)
GLOBAL_FUNC(atomic32_cas)

GLOBAL_FUNC(atomic64_get)
GLOBAL_FUNC(atomic64_set)
GLOBAL_FUNC(atomic64_inc)
GLOBAL_FUNC(atomic64_dec)
GLOBAL_FUNC(atomic64_cas)

.section .text
.code64

atomic32_get:
    movl        (%rdi), %eax
    ret

atomic32_set:
    xchgl       %esi, (%rdi)
    movl        %esi, %eax
    ret

atomic32_inc:
    movl        $1, %eax
    lock xaddl  %eax, (%rdi)
    ret

atomic32_dec:
    movl        $-1, %eax
    lock xaddl  %eax, (%rdi)
    ret

atomic32_cas:
    movl        %esi, %eax
    lock
    cmpxchgl    %edx, (%rdi)
    ret

atomic64_get:
    movq        (%rdi), %rax
    ret

atomic64_set:
    xchgq       %rsi, (%rdi)
    movq        %rsi, %rax
    ret

atomic64_inc:
    movq        $1, %rax
    lock xaddq  %rax, (%rdi)
    ret

atomic64_dec:
    movq        $-1, %rax
    lock xaddq  %rax, (%rdi)
    ret

atomic64_cas:
    movq        %rsi, %rax
    lock
    cmpxchgq    %rdx, (%rdi)
    ret
