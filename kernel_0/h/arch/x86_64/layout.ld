ENTRY(sys_boot)

KERNEL_LMA = 0x0000000001000000;
KERNEL_VMA = 0xffffffff81000000;

PHDRS {
    boot        PT_LOAD;
    trampoline  PT_LOAD;
    init        PT_LOAD;
    kernel      PT_LOAD;
}

SECTIONS {
    . = KERNEL_LMA;

    .boot KERNEL_LMA : AT(ADDR(.boot)) {
        *(.boot)
    } :boot =0x90

    _trampoline_addr = ALIGN(16) + KERNEL_VMA - KERNEL_LMA;
    .trampoline 0x7c000 : AT(_trampoline_addr - KERNEL_VMA + KERNEL_LMA) {
        KEEP(*(.trampoline))
    } :trampoline =0x90
    _trampoline_end = _trampoline_addr + SIZEOF(.trampoline);
    . = _trampoline_end;

    .percpu ALIGN(16) : AT(ADDR(.percpu) - KERNEL_VMA + KERNEL_LMA) {
        _percpu_addr = .;
        *(.percpu)
        _percpu_end = .;
    } :init =0

    .init ALIGN(16) : AT(ADDR(.init) - KERNEL_VMA + KERNEL_LMA) {
        *(.init.text)
        *(.init.data)
        _init_end = .;
    } :init =0x90

    .text ALIGN(0x1000) : AT(ADDR(.text) - KERNEL_VMA + KERNEL_LMA) {
        *(.text)
        *(.text.*)
        _text_end = .;
    } :kernel =0x90
    .rodata ALIGN(0x1000) : AT(ADDR(.rodata) - KERNEL_VMA + KERNEL_LMA) {
        *(.rodata)
        *(.rodata.*)
        _rodata_end = .;
    } :kernel =0
    .data ALIGN(0x1000) : AT(ADDR(.data) - KERNEL_VMA + KERNEL_LMA) {
        *(.data)
        *(.data.*)
        _data_end = .;
    } :kernel =0
    .bss ALIGN(16) (NOLOAD) : AT(ADDR(.bss) - KERNEL_VMA + KERNEL_LMA) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
        *(.allot)
        _bss_end = .;
    } :kernel

    /DISCARD/ : {
        *(.comment)
    }
}
