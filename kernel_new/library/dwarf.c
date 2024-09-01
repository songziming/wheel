#include "dwarf.h"
#include "string.h"
#include "debug.h"


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
    default:                        return "unknown            ";
    case DW_LNS_copy:               return "copy               ";
    case DW_LNS_advance_pc:         return "advance_pc         ";
    case DW_LNS_advance_line:       return "advance_line       ";
    case DW_LNS_set_file:           return "set_file           ";
    case DW_LNS_set_column:         return "set_column         ";
    case DW_LNS_negate_stmt:        return "negate_stmt        ";
    case DW_LNS_set_basic_block:    return "set_basic_block    ";
    case DW_LNS_const_add_pc:       return "const_add_pc       ";
    case DW_LNS_fixed_advance_pc:   return "fixed_advance_pc   ";
    case DW_LNS_set_prologue_end:   return "set_prologue_end   ";
    case DW_LNS_set_epilogue_begin: return "set_epilogue_begin ";
    case DW_LNS_set_isa:            return "set_isa            ";
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

// DWARF 文件里，整型字段使用 LEB128 变长编码
// 每个字节只有 7-bit 有效数字，最高位表示该字节是否为最后一个字节

static uint64_t decode_uleb128(dwarf_line_t *state) {
    uint64_t value = 0;
    int shift = 0;

    while (state->ptr < state->end) {
        uint8_t byte = *state->ptr++;
        value |= (uint64_t)(byte & 0x7f) << shift;
        shift += 7;
        if (0 == (byte & 0x80)) {
            return value;
        }
    }

    log("dwarf ULEB128 out-of-range\n");
    return 0;
}

#if 0
static int64_t decode_sleb128(dwarf_line_t *state) {
    int64_t value = 0;
    int shift = 0;

    while (state->ptr < state->end) {
        uint8_t byte = *state->ptr++;
        value |= (int64_t)(byte & 0x7f) << shift;
        shift += 7;
        if (0 == (byte & 0x80)) {
            if (byte & 0x40) {
                value |= -(1L << shift); // 如果是有符号数，需要符号扩展
            }
            return value;
        }
    }

    log("dwarf SLEB128 out-of-range\n");
    return 0;
}
#endif

static size_t decode_size(dwarf_line_t *state) {
    size_t addr = 0;
    memcpy(&addr, state->ptr, state->wordsize);
    state->ptr += state->wordsize;
    return addr;
}

static uint16_t decode_uhalf(dwarf_line_t *state) {
    uint16_t value;
    memcpy(&value, state->ptr, sizeof(uint16_t));
    state->ptr += sizeof(uint16_t);
    return value;
}

static size_t decode_initial_length(dwarf_line_t *state) {
    state->wordsize = sizeof(uint32_t);

    size_t length = 0;
    memcpy(&length, state->ptr, state->wordsize);
    state->ptr += state->wordsize;

    if (0xffffffff == length) {
        state->wordsize = sizeof(uint64_t);
        memcpy(&length, state->ptr, state->wordsize);
        state->ptr += state->wordsize;
    }

    return length;
}

// directory entry 与 filename entry 都可以用这个函数
static void parse_entry(dwarf_line_t *state, const uint64_t *format, uint8_t format_len) {
    for (int i = 0; i < format_len; ++i) {
        const char *key = show_type_code(format[2 * i]);
        uint64_t form = format[2*i+1];
        log("    ~ %s = ", key);

        switch (form) {
        case DW_FORM_string:
            log("%s\n", (const char *)state->ptr);
            state->ptr += strlen((const char *)state->ptr);
            break;
        case DW_FORM_strp: {
            size_t pos = decode_size(state);
            if (pos < state->str_size) {
                log("%s\n", state->str + pos);
            } else {
                log("null\n");
            }
            break;
        }
        case DW_FORM_line_strp: {
            size_t pos = decode_size(state);
            if (pos < state->line_str_size) {
                log("%s\n", state->line_str + pos);
            } else {
                log("null\n");
            }
            break;
        }
        case DW_FORM_data1:
            log("%d\n", *state->ptr++);
            break;
        case DW_FORM_data2:
            log("%d\n", decode_uhalf(state));
            break;
        case DW_FORM_udata:
            log("%ld\n", decode_uleb128(state));
            break;
        case DW_FORM_data16:
            for (int j = 0; j < 16; ++j) {
                log("%X", state->ptr[j]);
            }
            log("\n");
            state->ptr += 16;
            break;
        default:
            log("unsupported FORM %lu %s\n", form, show_format(form));
            break;
        }
    }
}






// line number information state machine registers
typedef struct line_num_state {
    uint64_t address;
    unsigned op_index;
    unsigned file;
    unsigned line;
    unsigned column;

    // 布尔类型，可以替换为 bitfield
    int      is_stmt;
    int      basic_block;
    int      end_sequence;
    int      prologue_end;
    int      epilogue_begin;

    unsigned isa;
    unsigned discriminator;
} line_num_state_t;

static void line_number_state_init(line_num_state_t *state) {
    memset(state, 0, sizeof(line_num_state_t));
    state->file = 1;
    state->line = 1;
}

// 解析一个 unit，返回下一个 unit 的地址
// uint8_t *data, const char *str, const char *line_str
static const uint8_t *parse_debug_line_unit(dwarf_line_t *dwarf) {
    const uint8_t *unit_start = dwarf->ptr;
    size_t unit_length = decode_initial_length(dwarf); // 同时更新 wordsize
    const uint8_t *end = dwarf->ptr + unit_length;

    line_num_state_t state;
    line_number_state_init(&state);

    uint16_t version = decode_uhalf(dwarf);
    if (5 != version) {
        log("we only support dwarf v5, but got %d\n", version);
        return end;
    }

    uint8_t address_size = *dwarf->ptr++;
    uint8_t segment_selector_size = *dwarf->ptr++;

    size_t header_length = decode_size(dwarf);
    const uint8_t *opcodes = dwarf->ptr + header_length;

    uint8_t min_inst_length = *dwarf->ptr++;
    uint8_t max_ops_per_inst = *dwarf->ptr++;
    uint8_t default_is_stmt = *dwarf->ptr++;
    int8_t  line_base = *(int8_t *)dwarf->ptr++;
    uint8_t line_range = *dwarf->ptr++;
    uint8_t opcode_base = *dwarf->ptr++;

    state.is_stmt = default_is_stmt;

    // 表示每个 opcode 各有多少个参数
    const uint8_t *standard_opcode_lengths = dwarf->ptr;
    dwarf->ptr += opcode_base - 1;

    log("- unit length %ld\n", unit_length);
    log("- version=%d, addr-size=%d, sel-size=%d\n", version, address_size, segment_selector_size);
    log("- header length %ld\n", header_length);
    log("- min_ins_len=%d, max_ops_per_inst=%d\n", min_inst_length, max_ops_per_inst);
    log("- default is_stmt=%d\n", default_is_stmt);
    log("- line_base=%d, line_range=%d, op_base=%d\n", line_base, line_range, opcode_base);

    log("- opcode nargs: [ %d", standard_opcode_lengths[0]);
    for (int i = 1; i + 1 < opcode_base; ++i) {
        log(", %d", standard_opcode_lengths[i]);
    }
    log(" ]\n");

    // format 数组应该是 LEB128，但合法取值都小于 128，理论上应该只占一个字节
    // 有些 user specific type code 取值可能超过一个字节，可以尝试 alloca
    // 不然这里就要动态申请内存，用来保存 format 字典
    uint8_t directory_entry_format_count = *dwarf->ptr++;
    uint64_t directory_formats[directory_entry_format_count * 2];
    for (int i = 0; i < directory_entry_format_count * 2; ++i) {
        directory_formats[i] = decode_uleb128(dwarf);
    }

    uint64_t directories_count = decode_uleb128(dwarf);
    log("- %d directory entries:\n", directories_count);
    for (unsigned i = 0; i < directories_count; ++i) {
        log("  * Directory #%d:\n", i);
        parse_entry(dwarf, directory_formats, directory_entry_format_count);
    }

    // LEB128，道理和 directory entry format 相同
    uint8_t file_name_entry_format_count = *dwarf->ptr++;
    uint64_t file_name_formats[file_name_entry_format_count * 2];
    for (int i = 0; i < file_name_entry_format_count * 2; ++i) {
        file_name_formats[i] = decode_uleb128(dwarf);
    }

    uint64_t file_name_count = decode_uleb128(dwarf);
    log("- %d file name entries:\n", file_name_count);
    for (unsigned i = 0; i < file_name_count; ++i) {
        log("  * File name #%d:\n", i);
        parse_entry(dwarf, file_name_formats, file_name_entry_format_count);
    }

    // 接下来应该是指令
    if (opcodes != dwarf->ptr) {
        log(">> current pointer %p, opcode %p\n", dwarf->ptr, opcodes);
    }

    // 解析执行指令，更新状态机
    log("- %d bytes of opcodes remaining:\n", (int)(end - opcodes));
    while (dwarf->ptr < end) {
        size_t rela = (size_t)(dwarf->ptr - unit_start);
        log("  [%lx] ", rela);

        uint8_t op = *dwarf->ptr++;

        // 特殊指令，没有参数
        if (op >= opcode_base) {
            op -= opcode_base;
            int op_adv = (int)op / line_range;
            int line_inc = (int)op % line_range + line_base;
            log("special %d: addr+=%d, line=%d\n", op, op_adv, line_inc);

            unsigned new_op = state.op_index + op_adv;
            state.op_index = new_op % max_ops_per_inst;
            state.address += min_inst_length * (new_op / max_ops_per_inst);
            continue;
        }

        // 扩展指令
        if (0 == op) {
            uint64_t nbytes = decode_uleb128(dwarf);
            uint8_t eop = *dwarf->ptr;
            dwarf->ptr += nbytes;
            log("extended %d: %s\n", eop, show_ext_opcode(eop));
            continue;
        }

        // 标准指令
        switch (op) {
        case DW_LNS_advance_pc:
            ASSERT(1 == standard_opcode_lengths[op - 1]);
            state.address += decode_uleb128(dwarf);
            // TODO 同时也要修改 op_index，行为细节和 special op 一样
            // TODO 可以把 special_op 的行为封装为函数，在这里直接调用
            break;
        case DW_LNS_advance_line:
            ASSERT(1 == standard_opcode_lengths[op - 1]);
            state.line += decode_uleb128(dwarf);
            break;
        case DW_LNS_set_file:
            ASSERT(1 == standard_opcode_lengths[op - 1]);
            state.file = decode_uleb128(dwarf);
            break;
        case DW_LNS_const_add_pc:
            ASSERT(0 == standard_opcode_lengths[op - 1]);
            // TODO 相当于 special op 255
            break;
        case DW_LNS_fixed_advance_pc:
            // 这个指令参数不是 LEB128，而是 uhalf
            state.address += decode_uhalf(dwarf);
            state.op_index = 0;
            break;
        case DW_LNS_copy:
            // TODO 在 addr2line 矩阵中增加一行，使用当前状态机取值
            break;
        default:
            log("standard %s\n", show_std_opcode(op));
            for (int i = 0; i < standard_opcode_lengths[op - 1]; ++i) {
                decode_uleb128(dwarf);
            }
            break;

        // case DW_LNS_negate_stmt:        return "negate_stmt        ";
        // case DW_LNS_set_basic_block:    return "set_basic_block    ";
        // case DW_LNS_set_prologue_end:   return "set_prologue_end   ";
        // case DW_LNS_set_epilogue_begin: return "set_epilogue_begin ";
        // case DW_LNS_set_isa:            return "set_isa            ";
        }
        
        // int nargs = standard_opcode_lengths[op - 1]; // 索引从 1 开始
        // for (int i = 0; i < nargs; ++i) {
        //     log(" %u", decode_uleb128(dwarf));
        // }
        // log("\n");
    }

    return end;
}

// 解析内核符号表时，根据 section 名称找到 debug_line、debug_str、debug_line_str 三个调试信息段
// 调用这个函数，解析行号信息，这样在断言失败、打印调用栈时可以显示对应的源码位置，而不只是所在函数
// 本函数在开机阶段调用一次，将行号信息保存下来
void parse_debug_line(dwarf_line_t *line) {
    const char *hr = "====================";

    for (int i = 0; line->ptr < line->end; ++i) {
        log("%s%s%s%s\n", hr, hr, hr, hr);
        log("Debug Line Unit #%d:\n", i);
        line->ptr = parse_debug_line_unit(line);
    }
    log("%s%s%s%s\n", hr, hr, hr, hr);
}

// 寻找指定内存地址对应的源码位置
void show_line_info() {
    // TODO
}
