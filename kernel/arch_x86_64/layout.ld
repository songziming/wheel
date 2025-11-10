REAL_ADDR = 0x0000000000008000; /* 32K AP 实模式引导代码的地址 */
LOAD_ADDR = 0x0000000000100000; /*  1M 内核加载位置 */
TEXT_ADDR = 0xffffffff80000000; /* -2G 内核代码虚拟地址偏移 */

ENTRY(sys_boot)

PHDRS {
    boot        PT_LOAD;
}

/* NOLOAD section 必须位于 segment 末尾 */

SECTIONS {
    . = LOAD_ADDR;

    /* 引导阶段的页表、数据段、代码段 */
    /* 其中数据段含 multiboot header */
    .boot.data : { KEEP(*(.boot.data)) } : boot = 0
    .boot.text : { *(.boot.text) } : boot = 0x90
    .boot.bss (NOLOAD) : { *(.boot.bss) } : boot

    /* 后面的位于高地址 */
    . += TEXT_ADDR;

    /DISCARD/ : {
        *(.comment)
    }
}
