// 引导模块，从 GRUB 接过控制权


.global sys_boot
.global sys_boot_ap
.global mb_eax

.extern sys_init


#include <arch_config.h>
#include <init/multiboot1.h>
#include <init/multiboot2.h>


//------------------------------------------------------------------------------
// multiboot 引导头
//------------------------------------------------------------------------------

.section ".boot.data", "aw"

#define MB1_HEADER_FLAGS (MB1_HEADER_PAGE_ALIGN + MB1_HEADER_MEMORY_INFO)
#define MB1_HEADER_CHECK (0 - MB1_HEADER_MAGIC - MB1_HEADER_FLAGS)
#define MB2_HEADER_LEN   (mb2_header_end - mb2_header)
#define MB2_HEADER_CHECK (0 - MB2_HEADER_MAGIC - MB2_ARCHITECTURE_I386 - MB2_HEADER_LEN)

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
    .long   8 + 5 * 4
    .long   MB2_TAG_TYPE_MMAP
    .long   MB2_TAG_TYPE_VBE
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

.balign 16, 0
mb_eax: .long 0
mb_ebx: .long 0



//------------------------------------------------------------------------------
// 32-bit 代码，运行在保护模式，低地址
//------------------------------------------------------------------------------

.section ".boot.text", "ax"
.code32

sys_boot:
    // 关闭中断（重置 eflags，使用页表空间为临时栈）
    movl    $pd3 + 0x1000, %esp
    pushl   $2
    popfl

    // 保存 grub 信息
    movl    %eax, (mb_eax)
    movl    %ebx, (mb_ebx)

    // 关分页
    movl    %cr0, %eax
    andl    $0x7fffffff, %eax
    movl    %eax, %cr0

    // 准备启动阶段的页表，包含三段地址映射：
    //  1. 0~4G 映射到物理地址 0~4G
    //  2. Canonical hole 之后的 4G 映射到物理地址 0~4G
    //  3. 虚拟地址最后 2G 映射到物理地址 0~2G
    // 使用 2M page，以节省页表大小

    // 页表清零
    movl    $pml4, %edi
    movl    $0x7000, %ecx
    xorl    %eax, %eax
    rep stosb

    // 准备 PML4
    movl    $pml4, %edi
    xorl    %edx, %edx
    movl    $pdp0 + 3, (%edi, %edx, 8)  // pml4[0]   -> pdp0
    movl    $256, %edx
    movl    $pdp0 + 3, (%edi, %edx, 8)  // pml4[256] -> pdp0
    movl    $511, %edx
    movl    $pdp1 + 3, (%edi, %edx, 8)  // pml4[511] -> pdp1

    // 准备第一张 PDP
    movl    $pdp0, %edi
    xorl    %edx, %edx
    movl    $pd0 + 3, (%edi, %edx, 8)   // pdp0[0]   -> pd0
    incl    %edx
    movl    $pd1 + 3, (%edi, %edx, 8)   // pdp0[1]   -> pd1
    incl    %edx
    movl    $pd2 + 3, (%edi, %edx, 8)   // pdp0[2]   -> pd2
    incl    %edx
    movl    $pd3 + 3, (%edi, %edx, 8)   // pdp0[3]   -> pd3

    // 准备第二张 PDP
    movl    $pdp1, %edi
    movl    $510, %edx
    movl    $pd0 + 3, (%edi, %edx, 8)   // pdp1[510] -> pd0
    movl    $511, %edx
    movl    $pd1 + 3, (%edi, %edx, 8)   // pdp1[511] -> pd1

    // 准备 4 张 PD
    movl    $0x87, %eax
    movl    $pd0, %edi
    movl    $2048, %ecx
.pde:
    movl    %eax, (%edi)
    addl    $0x200000, %eax
    addl    $8, %edi
    loop    .pde

// AP 从这里启动
sys_boot_ap:
    // 加载临时页表
    movl    $pml4, %eax
    movl    %eax, %cr3

    // 开启 PAE
    movl    %cr4, %eax
    orl     $1 << 5, %eax
    movl    %eax, %cr4

    // 开启 EFER.LME
    movl    $0xc0000080, %ecx
    rdmsr
    orl     $1 << 8, %eax
    wrmsr

    // 开启分页，进入 long mode
    movl    %cr0, %eax
    orl     $1 << 31, %eax
    movl    %eax, %cr0

    // 此时处于 compatibility mode
    // 加载新的 GDT，跳转到 64-bit 代码段，进入 64-bit mode
    lgdt    gdtr64
    ljmp    $8, $lower64
1:
    hlt
    jmp     1b



//------------------------------------------------------------------------------
// 64-bit mode，低地址
//------------------------------------------------------------------------------

.code64

lower64:
    movw    $16, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    jmp     higher64
2:
    hlt
    jmp     2b



//------------------------------------------------------------------------------
// 为页表预留的空间，处于低地址
//------------------------------------------------------------------------------

.section ".boot.bss", "aw", @nobits

.balign 0x1000
pml4: .skip 0x1000
pdp0: .skip 0x1000
pdp1: .skip 0x1000
pd0:  .skip 0x1000
pd1:  .skip 0x1000
pd2:  .skip 0x1000
pd3:  .skip 0x1000



//------------------------------------------------------------------------------
// 64-bit mode，高地址
//------------------------------------------------------------------------------

.section ".init.text", "ax"
.code64

higher64:
    movl    (mb_eax), %edi
    movl    (mb_ebx), %esi

    // 使用 jmp 模拟 call，返回地址为零
    movq    $init_stack_top, %rsp
    pushq   $0
    jmp     sys_init
3:
    hlt
    jmp     3b



//------------------------------------------------------------------------------
// 初始化阶段使用的临时栈，开启调度之后弃用
//------------------------------------------------------------------------------

.section ".init.bss", "aw", @nobits

.balign 16
init_stack:
    .skip   INIT_STACK_SIZE
init_stack_top:
