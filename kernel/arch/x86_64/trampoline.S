// located at 0x7c000, executed under real mode (16-bit)
// switch to protected mode (32-bit), and jump to `sys_entry`

.section .trampoline, "ax"
.code16

trampoline_entry:
    cli
    movw    %cs, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    // enable a20 address line
    inb     $0x92, %al
    orb     $2, %al
    andb    $0xfe, %al
    outb    %al, $0x92

    // load protected mode gdt
    lgdtl   gdtr32 - trampoline_entry

    // enable protected mode
    movl    %cr0, %eax
    orl     $1, %eax
    andl    $0x7fffffff, %eax
    movl    %eax, %cr0

    // load selector for kernel code segment
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
    .byte   0xcf        //         64-bit, and limit [16:19]
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

.code32

start32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    // we use 0xdeadbeef as the magic value for ap startup
    // and jump to KERNEL_LMA, executing from `sys_entry`
    movl    $0xdeadbeef, %eax
    jmp     0x1000000

die32:
    hlt
    jmp     die32
