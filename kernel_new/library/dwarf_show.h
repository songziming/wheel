#include "dwarf.h"

static const char *show_type_code(type_code_t code) {
    switch (code) {
    default: return "other-type";
    case DW_LNCT_path:            return "path";
    case DW_LNCT_directory_index: return "directory_index";
    case DW_LNCT_timestamp:       return "timestamp";
    case DW_LNCT_size:            return "size";
    case DW_LNCT_MD5:             return "MD5";
    }
}

static const char *show_format(form_t form) {
    switch (form) {
    default:                     return "unknown";
    case DW_FORM_addr:           return "addr";
    case DW_FORM_block2:         return "block2";
    case DW_FORM_block4:         return "block4";
    case DW_FORM_data2:          return "data2";
    case DW_FORM_data4:          return "data4";
    case DW_FORM_data8:          return "data8";
    case DW_FORM_string:         return "string";
    case DW_FORM_block:          return "block";
    case DW_FORM_block1:         return "block1";
    case DW_FORM_data1:          return "data1";
    case DW_FORM_flag:           return "flag";
    case DW_FORM_sdata:          return "sdata";
    case DW_FORM_strp:           return "strp";
    case DW_FORM_udata:          return "udata";
    case DW_FORM_ref_addr:       return "ref_addr";
    case DW_FORM_ref1:           return "ref1";
    case DW_FORM_ref2:           return "ref2";
    case DW_FORM_ref4:           return "ref4";
    case DW_FORM_ref8:           return "ref8";
    case DW_FORM_ref_udata:      return "ref_udata";
    case DW_FORM_indirect:       return "indirect";
    case DW_FORM_sec_offset:     return "sec_offset";
    case DW_FORM_exprloc:        return "exprloc";
    case DW_FORM_flag_present:   return "flag_present";
    case DW_FORM_strx:           return "strx";
    case DW_FORM_addrx:          return "addrx";
    case DW_FORM_ref_sup4:       return "ref_sup4";
    case DW_FORM_strp_sup:       return "strp_sup";
    case DW_FORM_data16:         return "data16";
    case DW_FORM_line_strp:      return "line_strp";
    case DW_FORM_ref_sig8:       return "ref_sig8";
    case DW_FORM_implicit_const: return "implicit_const";
    case DW_FORM_loclistx:       return "loclistx";
    case DW_FORM_rnglistx:       return "rnglistx";
    case DW_FORM_ref_sup8:       return "ref_sup8";
    case DW_FORM_strx1:          return "strx1";
    case DW_FORM_strx2:          return "strx2";
    case DW_FORM_strx3:          return "strx3";
    case DW_FORM_strx4:          return "strx4";
    case DW_FORM_addrx1:         return "addrx1";
    case DW_FORM_addrx2:         return "addrx2";
    case DW_FORM_addrx3:         return "addrx3";
    case DW_FORM_addrx4:         return "addrx4";
    }
}

static const char *show_std_opcode(std_opcode_t op) {
    switch (op) {
    default:                        return "unknown";
    case DW_LNS_copy:               return "copy";
    case DW_LNS_advance_pc:         return "advance_pc";
    case DW_LNS_advance_line:       return "advance_line";
    case DW_LNS_set_file:           return "set_file";
    case DW_LNS_set_column:         return "set_column";
    case DW_LNS_negate_stmt:        return "negate_stmt";
    case DW_LNS_set_basic_block:    return "set_basic_block";
    case DW_LNS_const_add_pc:       return "const_add_pc";
    case DW_LNS_fixed_advance_pc:   return "fixed_advance_pc";
    case DW_LNS_set_prologue_end:   return "set_prologue_end";
    case DW_LNS_set_epilogue_begin: return "set_epilogue_begin";
    case DW_LNS_set_isa:            return "set_isa";
    }
}

static const char *show_ext_opcode(ext_opcode_t op) {
    switch (op) {
    default:                       return "unknown";
    case DW_LNE_end_sequence:      return "end_sequence";
    case DW_LNE_set_address:       return "set_address";
    case DW_LNE_set_discriminator: return "set_discriminator";
    case DW_LNE_lo_user:           return "lo_user";
    case DW_LNE_hi_user:           return "hi_user";
    }
}
