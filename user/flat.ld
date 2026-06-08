OUTPUT_FORMAT(binary)
SECTIONS {
    . = 0x400000;          /* 固定加载地址 4M */
    .text : { *(.text) }
    .data : { *(.data) }
    .bss  : { *(.bss) }
}