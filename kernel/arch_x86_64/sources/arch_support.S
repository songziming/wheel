// 一些支持函数


.global load_gdtr
.global load_idtr
.global load_tr

.section ".init.text", "ax"
.code64

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