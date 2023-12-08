.global load_gdtr
.global load_idtr
.global load_tr

.section ".init.text", "ax"
.code64

// 加载 GDTR 并加载新的内核代码段、内核数据段
// FS、GS 保持不变，这两个段寄存器有特殊含义
load_gdtr:
    lgdtq   (%rdi)
    pushq   $8
    pushq   $_refresh
    lretq
_refresh:
    movw    $16, %ax
    movw    %ax, %ds
    movw    %ax, %es
    // movw    %ax, %fs
    // movw    %ax, %gs
    movw    %ax, %ss
    ret

load_idtr:
    lidt    (%rdi)
    ret

load_tr:
    ltr     %di
    ret
