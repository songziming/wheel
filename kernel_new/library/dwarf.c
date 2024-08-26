#include "dwarf.h"
#include "string.h"
#include "debug.h"


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



// static size_t form_size(form_t form) {
//     switch (form) {
//     default:
//         log("unsupported form %s\n", show_format(form));
//         return 0;
//     case DW_FORM_string
//     }
// }


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
static void parse_entry(dwarf_line_t *state, const uint8_t *format, uint8_t format_len) {
    for (int i = 0; i < format_len; ++i) {
        const char *key = show_type_code(format[2 * i]);
        log("    ~ %s = ", key);

        switch (format[2 * i + 1]) {
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
            log("unsupported FORM %s\n", show_format(format[2 * i + 1]));
            break;
        }
    }
}

// 解析一个 unit，返回下一个 unit 的地址
// uint8_t *data, const char *str, const char *line_str
static const uint8_t *parse_debug_line_unit(dwarf_line_t *state) {
    size_t unit_length = decode_initial_length(state); // 同时更新 wordsize
    const uint8_t *end = state->ptr + unit_length;

    uint16_t version = decode_uhalf(state);
    if (5 != version) {
        log("we only support dwarf v5, but got %d\n", version);
        return end;
    }

    uint8_t address_size = *state->ptr++;
    uint8_t segment_selector_size = *state->ptr++;

    size_t header_length = decode_size(state);
    const uint8_t *opcodes = state->ptr + header_length;

    uint8_t minimum_instruction_length = *state->ptr++;
    uint8_t maximum_operations_per_instruction = *state->ptr++;
    uint8_t default_is_stmt = *state->ptr++;
    int8_t  line_base = *(int8_t *)state->ptr++;
    uint8_t line_range = *state->ptr++;
    uint8_t opcode_base = *state->ptr++;

    // 表示每个 opcode 各有多少个参数
    const uint8_t *standard_opcode_lengths = state->ptr;
    state->ptr += opcode_base - 1;

    log("- unit length %ld\n", unit_length);
    log("- version=%d, addr-size=%d, sel-size=%d\n", version, address_size, segment_selector_size);
    log("- header length %ld\n", header_length);
    log("- min_ins_len=%d, max_ops_per_ins=%d\n", minimum_instruction_length, maximum_operations_per_instruction);
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
    uint8_t directory_entry_format_count = *state->ptr++;
    const uint8_t *directory_formats = state->ptr;
    state->ptr += directory_entry_format_count * 2;

    // uint8_t directories_count = *state->ptr++;
    uint64_t directories_count = decode_uleb128(state);
    log("- %d directory entries:\n", directories_count);
    for (unsigned i = 0; i < directories_count; ++i) {
        log("  * Directory #%d:\n", i);
        parse_entry(state, directory_formats, directory_entry_format_count);
    }

    // LEB128，道理和 directory entry format 相同
    uint8_t file_name_entry_format_count = *state->ptr++;
    const uint8_t *file_name_formats = state->ptr;
    state->ptr += file_name_entry_format_count * 2;

    // uint8_t file_name_count = *state->ptr++;
    uint64_t file_name_count = decode_uleb128(state);
    log("- %d file name entries:\n", file_name_count);
    for (unsigned i = 0; i < file_name_count; ++i) {
        log("  * File name #%d:\n", i);
        parse_entry(state, file_name_formats, file_name_entry_format_count);
    }

    // 解析执行指令
    log("- %d bytes of opcodes remaining\n", (int)(end - opcodes));
    while (opcodes < end) {
        uint8_t op = *opcodes++;

        if (op >= opcode_base) {
            continue; // 扩展指令
        }

        // 解析标准指令
        switch (op) {
        case DW_LNS_copy:           break;
        case DW_LNS_advance_pc:     break;
        case DW_LNS_advance_line:   break;
        case DW_LNS_set_file:       break;
        case DW_LNS_set_column:     break;
        }
    }

    return end;
}

// 解析内核符号表时，根据 section 名称找到 debug_line、debug_str、debug_line_str 三个调试信息段
// 调用这个函数，解析行号信息，这样在断言失败、打印调用栈时可以显示对应的源码位置，而不只是所在函数
// 本函数在开机阶段调用一次，将行号信息保存下来
void parse_debug_line(dwarf_line_t *line) {
    const char *hr = "---------------------------------\n";

    for (int i = 0; line->ptr < line->end; ++i) {
        log(hr);
        log("Debug Line Unit #%d:\n", i);
        line->ptr = parse_debug_line_unit(line);
    }
    log(hr);
}

// 寻找指定内存地址对应的源码位置
void show_line_info() {
    // TODO
}
