// 实模式启动代码，用于多核环境下启动 AP

.extern mb_eax
.extern sys_boot_ap

.section .real, "ax"
.code16

real_entry:
    cli
    movw    %cs, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    // 加载保护模式 GDT
    lgdtl   gdtr32 - real_entry

    // 开启保护模式
    movl    %cr0, %eax
    orl     $1, %eax
    andl    $0x7fffffff, %eax
    movl    %eax, %cr0

    // 跳转到保护模式代码段
    ljmpl   $8, $start32

die16:
    hlt
    jmp     die16

.balign 16
gdt32:
    .long   0           // dummy descriptor
    .long   0

    .word   0xffff      // code 0: limit [0 :15]
    .word   0           //         base  [0 :15]
    .byte   0           //         base  [16:23]
    .byte   0x9a        //         Present, DPL=0, non-conforming
    .byte   0xcf        //         G=1, 32-bit, and limit [16:19]
    .byte   0           //         base  [24:31]

    .word   0xffff      // data 0: limit [0 :15]
    .word   0           //         base  [0 :15]
    .byte   0           //         base  [16:23]
    .byte   0x92        //         Present, DPL=0, writable
    .byte   0xcf        //         G=1, 4GB, and limit [16:19]
    .byte   0           //         base  [24:31]

.balign 16
gdtr32:
    .word   3 * 8 - 1
    .long   gdt32

.code32

start32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    // 跳转到 multiboot 入口点
    movl    $0xdeadbeef, (mb_eax)
    jmp     sys_boot_ap

die32:
    hlt
    jmp     die32
