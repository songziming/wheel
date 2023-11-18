// 引导模块，从 GRUB 接过控制权


.global sys_boot


#include <multiboot1.h>
#include <multiboot2.h>
#include <arch_config.h>


#define MB1_HEADER_FLAGS (MB1_HEADER_PAGE_ALIGN + MB1_HEADER_MEMORY_INFO)
#define MB1_HEADER_CHECK (0 - MB1_HEADER_MAGIC - MB1_HEADER_FLAGS)
#define MB2_HEADER_LEN   (mb2_header_end - mb2_header)
#define MB2_HEADER_CHECK (0 - MB2_HEADER_MAGIC - MB2_ARCHITECTURE_I386 - MB2_HEADER_LEN)


.section ".boot.data", "aw"

.balign 4, 0
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
.info_req_tag:
    .word   MB2_HEADER_TAG_INFORMATION_REQUEST
    .word   0
    .long   8 + 4 * 4
    .long   MB2_TAG_TYPE_MMAP
    .long   MB2_TAG_TYPE_ELF_SECTIONS
    .long   MB2_TAG_TYPE_ACPI_OLD
    .long   MB2_TAG_TYPE_ACPI_NEW
.balign 8, 0
.framebuffer_tag:
    .word   MB2_HEADER_TAG_FRAMEBUFFER
    .word   0
    .long   20
    .long   1024    // width
    .long   768     // height
    .long   32      // depth
.balign 8, 0
.end_tag:
    .word   MB2_HEADER_TAG_END
    .word   0
    .long   8
mb2_header_end:



.section ".boot.text", "ax"
.code32


sys_boot:
    movb    $'B', %al
    movb    $0x1f, %ah
    movl    $0xb8000, %edi
    movw    %ax, (%edi)

.die:
    hlt
    jmp     .die
