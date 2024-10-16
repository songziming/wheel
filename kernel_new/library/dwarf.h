#ifndef DWARF_H
#define DWARF_H

#include <common.h>

// 以 DWARF v5 为准

// line number content type code
typedef enum dwarf_content_type {
    DW_LNCT_path            = 0x01,
    DW_LNCT_directory_index = 0x02,
    DW_LNCT_timestamp       = 0x03,
    DW_LNCT_size            = 0x04,
    DW_LNCT_MD5             = 0x05,
    DW_LNCT_lo_user         = 0x2000,
    DW_LNCT_hi_user         = 0x3fff,
} dwarf_content_type_t;

// attribute form encodings
typedef enum dwarf_form {
    DW_FORM_addr            = 0x01,
    DW_FORM_block2          = 0x03,
    DW_FORM_block4          = 0x04,
    DW_FORM_data2           = 0x05,
    DW_FORM_data4           = 0x06,
    DW_FORM_data8           = 0x07,
    DW_FORM_string          = 0x08,
    DW_FORM_block           = 0x09,
    DW_FORM_block1          = 0x0a,
    DW_FORM_data1           = 0x0b,
    DW_FORM_flag            = 0x0c,
    DW_FORM_sdata           = 0x0d,
    DW_FORM_strp            = 0x0e,
    DW_FORM_udata           = 0x0f,
    DW_FORM_ref_addr        = 0x10,
    DW_FORM_ref1            = 0x11,
    DW_FORM_ref2            = 0x12,
    DW_FORM_ref4            = 0x13,
    DW_FORM_ref8            = 0x14,
    DW_FORM_ref_udata       = 0x15,
    DW_FORM_indirect        = 0x16,
    DW_FORM_sec_offset      = 0x17,
    DW_FORM_exprloc         = 0x18,
    DW_FORM_flag_present    = 0x19,
    DW_FORM_strx            = 0x1a,
    DW_FORM_addrx           = 0x1b,
    DW_FORM_ref_sup4        = 0x1c,
    DW_FORM_strp_sup        = 0x1d,
    DW_FORM_data16          = 0x1e,
    DW_FORM_line_strp       = 0x1f,
    DW_FORM_ref_sig8        = 0x20,
    DW_FORM_implicit_const  = 0x21,
    DW_FORM_loclistx        = 0x22,
    DW_FORM_rnglistx        = 0x23,
    DW_FORM_ref_sup8        = 0x24,
    DW_FORM_strx1           = 0x25,
    DW_FORM_strx2           = 0x26,
    DW_FORM_strx3           = 0x27,
    DW_FORM_strx4           = 0x28,
    DW_FORM_addrx1          = 0x29,
    DW_FORM_addrx2          = 0x2a,
    DW_FORM_addrx3          = 0x2b,
    DW_FORM_addrx4          = 0x2c,
} dwarf_form_t;

// line number standard opcode encodings
typedef enum std_opcode {
    DW_LNS_copy                 = 0x01,
    DW_LNS_advance_pc           = 0x02,
    DW_LNS_advance_line         = 0x03,
    DW_LNS_set_file             = 0x04,
    DW_LNS_set_column           = 0x05,
    DW_LNS_negate_stmt          = 0x06,
    DW_LNS_set_basic_block      = 0x07,
    DW_LNS_const_add_pc         = 0x08,
    DW_LNS_fixed_advance_pc     = 0x09,
    DW_LNS_set_prologue_end     = 0x0a,
    DW_LNS_set_epilogue_begin   = 0x0b,
    DW_LNS_set_isa              = 0x0c,
} std_opcode_t;

// line number program extended opcodes
typedef enum ext_opcode {
    DW_LNE_end_sequence      = 0x01,
    DW_LNE_set_address       = 0x02,
    DW_LNE_set_discriminator = 0x04,
    DW_LNE_lo_user           = 0x80,
    DW_LNE_hi_user           = 0xff,
} ext_opcode_t;

// dwarf 调试信息，来自 elf sections
typedef struct dwarf_line {
    const uint8_t *line;        // .debug_line
    const uint8_t *line_end;
    const char    *str;         // .debug_str
    size_t         str_size;
    const char    *line_str;    // .debug_line_str
    size_t         line_str_size;
} dwarf_line_t;

int addr_to_line(size_t target, const char **file);

INIT_TEXT void parse_debug_line(dwarf_line_t *line);

#endif // DWARF_H
