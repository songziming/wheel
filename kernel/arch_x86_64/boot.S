#include "multiboot1.h"
#include "multiboot2.h"

#define MB1_HEADER_FLAGS (MB1_HEADER_PAGE_ALIGN + MB1_HEADER_MEMORY_INFO)
#define MB1_HEADER_CHECK (0 - MB1_HEADER_MAGIC - MB1_HEADER_FLAGS)
#define MB2_HEADER_LEN   (mb2_header_end - mb2_header)
#define MB2_HEADER_CHECK (0 - MB2_HEADER_MAGIC - MB2_ARCHITECTURE_I386 - MB2_HEADER_LEN)

.global sys_boot


//------------------------------------------------------------------------------
// multiboot 引导头、临时 GDT，lower-half
//------------------------------------------------------------------------------

.section ".boot.data", "a"

.balign 4,0
mb1_header:
    .long   MB1_HEADER_MAGIC
    .long   MB1_HEADER_FLAGS
    .long   MB1_HEADER_CHECK

.balign 8, 0
mb2_header:
    .long   MB2_HEADER_MAGIC
    .long   MB2_ARCHITECTURE_I386
    .long   MB2_HEADER_LEN
    .long   MB2_HEADER_CHECK
.balign 8, 0
    .word   MB2_HEADER_TAG_INFORMATION_REQUEST
    .word   0
    .long   8 + 4 * 4
    .long   MB2_TAG_TYPE_MMAP
    .long   MB2_TAG_TYPE_ELF_SECTIONS
    .long   MB2_TAG_TYPE_ACPI_OLD
    .long   MB2_TAG_TYPE_ACPI_NEW
.balign 8, 0
    .word   MB2_HEADER_TAG_MODULE_ALIGN
    .word   0
    .long   8
.balign 8, 0
    .word   MB2_HEADER_TAG_FRAMEBUFFER
    .word   0
    .long   20
    .long   1024    // width
    .long   768     // height
    .long   32      // depth
.balign 8, 0
    .word   MB2_HEADER_TAG_END
    .word   0
    .long   8
mb2_header_end:

.balign 16, 0
gdt64:
    .quad   0       // dummy descriptor
    .word   0       // code 0: limit [0 :15]
    .word   0       //         base  [0 :15]
    .byte   0       //         base  [16:23]
    .byte   0x98    //         P=1, DPL=0, C=0
    .byte   0x20    //         D=0, L=1, limit [16:19]
    .byte   0       //         base  [24:31]
    .word   0       // data 0: limit [0 :15]
    .word   0       //         base  [0 :15]
    .byte   0       //         base  [16:23]
    .byte   0x92    //         P=1, DPL=0, W=1
    .byte   0       //         limit [16:19]
    .byte   0       //         base  [24:31]

.balign 16, 0
gdtr64:
    .word   3 * 8 - 1
    .quad   gdt64


//------------------------------------------------------------------------------
// 引导入口，32-bit 保护模式，lower-half
//------------------------------------------------------------------------------

.section ".boot.text", "ax"
.code32

sys_boot:
    cli

    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs

    movw    %ax, %ds
    movb    $'B', %al
    movb    $0x1f, %ah
    movl    $0xb8000, %edi
    movw    %ax, (%edi)
1:
    hlt
    jmp     1b
