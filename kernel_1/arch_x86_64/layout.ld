LOAD_ADDR = 0x0000000000100000; /*  1M 内核加载位置 */
TEXT_BASE = 0xffffffff80000000; /* -2G 内核代码虚拟地址偏移 */
REAL_ADDR = 0x0000000000008000; /* 32K AP 实模式引导代码的地址 */

ENTRY(sys_boot)

PHDRS {
    boot        PT_LOAD;
    pcpu        PT_LOAD;
    real        PT_LOAD;
    init        PT_LOAD;
    text        PT_LOAD;
    rodata      PT_LOAD;
    data        PT_LOAD;
}

/*
NOLOAD section 必须位于 segment 末尾
*/

SECTIONS {
    . = LOAD_ADDR;

    /* 引导阶段的页表、数据段、代码段 */
    /* 其中数据段含 multiboot header */
    .boot.data : { *(.boot.data) } : boot = 0
    .boot.text : { *(.boot.text) } : boot = 0x90
    .boot.bss (NOLOAD) : { *(.boot.bss) } : boot

    /* 后面的位于高地址 */
    . += TEXT_BASE;

    /* per cpu 模板，会被复制多份（取决于 CPU 个数） */
    _pcpu_addr = .;
    .pcpu.data : AT(ADDR(.pcpu.data) - TEXT_BASE) { *(.pcpu.data) } : pcpu = 0x90
    _pcpu_data_end = .;
    .pcpu.bss (NOLOAD) : AT(ADDR(.pcpu.bss) - TEXT_BASE) { *(.pcpu.bss) } : pcpu
    _pcpu_bss_end = .;

    /* 实模式启动代码模板，会被复制到 1M 以下，用于启动其他 CPU */
    _real_addr = .;
    .real REAL_ADDR : AT(_real_addr - TEXT_BASE) { KEEP(*(.real)) } : real = 0x90
    _real_end = _real_addr + SIZEOF(.real);
    . = _real_end;

    /* 初始化阶段的代码和数据 */
    .init.text : AT(ADDR(.init.text) - TEXT_BASE) { *(.init.text) } : init = 0x90
    .init.data : AT(ADDR(.init.data) - TEXT_BASE) { *(.init.data) } : init = 0
    .init.bss (NOLOAD) : AT(ADDR(.init.bss) - TEXT_BASE) { *(.init.bss) } : init
    _init_end = .;

    .text ALIGN(4K) : AT(ADDR(.text) - TEXT_BASE) {
        _text_addr = .;
        *(.text)
        *(.text.*)
        _text_end = .;
    } : text = 0x90

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - TEXT_BASE) {
        _rodata_addr = .;
        *(.rodata)
        *(.rodata.*)
        *(.rotail)
        _rodata_end = .;
    } : rodata = 0
    .rotail (NOLOAD) : AT(ADDR(.rotail) - TEXT_BASE) {
        *(.rotail)
    } : rodata = 0

    .data ALIGN(4K) : AT(ADDR(.data) - TEXT_BASE) {
        _data_addr = .;
        *(.data)
        *(.data.*)
    } : data
    .bss (NOLOAD) : AT(ADDR(.bss) - TEXT_BASE) {
        *(.bss)
        *(.bss.*)
        *(.tail)
    } : data

    /DISCARD/ : {
        *(.comment)
    }
}
