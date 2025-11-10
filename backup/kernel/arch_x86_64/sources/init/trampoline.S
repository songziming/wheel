// AP 实模式初始化代码

#include <arch_config.h>

.extern mb_eax
.extern sys_boot_ap

.section ".real", "ax"
.code16

real_entry:
    cli
    movw    %cs, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    // TODO 还有没有必要开启 A20？

    // 加载保护模式 GDT
    lgdtl   gdtr32 - real_entry

    // 切换保护模式，禁用分页
    movl    %cr0, %eax
    orl     $1, %eax
    andl    $0x7fffffff, %eax
    movl    %eax, %cr0

    // 加载代码段描述子，开始执行 32-bit 代码
    ljmpl   $8, $start32

1:
    hlt
    jmp     1b


// 32 位保护模式 GDT
.balign 16
gdt32:
    .quad   0           // dummy descriptor
    .word   0xffff      // code 0: limit [0 :15]
    .word   0           //         base  [0 :15]
    .byte   0           //         base  [16:23]
    .byte   0x9a        //         Present, DPL=0, non-conforming
    .byte   0xcf        //         32-bit, and limit [16:19]
    .byte   0           //         base  [24:31]
    .word   0xffff      // data 0: limit [0 :15]
    .word   0           //         base  [0 :15]
    .byte   0           //         base  [16:23]
    .byte   0x92        //         Present, DPL=0, writable
    .byte   0xcf        //         attr and limit [16:19]
    .byte   0           //         base  [24:31]

.balign 16
gdtr32:
    .word   3 * 8 - 1
    .long   gdt32


// 后面是运行在 32-bit 保护模式下的代码
.code32

start32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    // 直接跳转到加载临时页表的部分
    movl    $AP_BOOT_MAGIC, (mb_eax)
    jmp     sys_boot_ap

2:
    hlt
    jmp     2b
