ENTRY(_start)

SECTIONS {
    . = 0x400000; /* 固定加载地址 4M */

    .text ALIGN(4K) : {
        *(.head)
        *(.text)
    }

    .rodata ALIGN(4K) : { *(.rodata) }
    .data ALIGN(4K) : { *(.data) }
    .bss ALIGN(4K) (NOLOAD) : { *(.bss) }
}
