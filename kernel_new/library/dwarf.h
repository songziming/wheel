#ifndef DWARF_H
#define DWARF_H

#include <common.h>

// 以 DWARF v5 为准
// TODO Dwarf 有没有官方的类型定义头文件？


enum unit_header_type {
    DW_UT_compile       = 0x01,
    DW_UT_type          = 0x02,
    DW_UT_partial       = 0x03,
    DW_UT_skeleton      = 0x04,
    DW_UT_split_compile = 0x05,
    DW_UT_split_type    = 0x06,
    DW_UT_lo_user       = 0x80,
    DW_UT_hi_user       = 0xff,
};

typedef struct initial_length {
    uint32_t fixed; // 0xffffffff
    uint64_t length;
} PACKED initial_length_t;

#endif // DWARF_H
