.global atomic32_add

.text

atomic32_add:
    lock xaddl  %esi, (%rdi)
    movl        %esi, %eax
    ret

