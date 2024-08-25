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


// 应该创建一个 directory 对象，方便后面解析过程引用
// filename 格式与 directory 类似，也用这个函数解析
static uint8_t *parse_directory_entry(uint8_t *data, const uint8_t *format, uint8_t format_len, size_t wordsize, const char *dbg_str, const char *dbg_line_str) {
    for (int i = 0; i < format_len; ++i) {
        const char *key = show_type_code(format[2 * i]);
        log("    %s = ", key);

        // const char *val = NULL;
        size_t offset = 0;

        switch (format[2 * i + 1]) {
        case DW_FORM_string:
            log("%s\n", (const char *)data);
            data += strlen((const char *)data);
            continue;
        case DW_FORM_strp:
            // val = dbg_str;
            memcpy(&offset, data, wordsize);
            log("%s\n", dbg_str + offset);
            data += wordsize;
            break;
        case DW_FORM_line_strp:
            // val = dbg_line_str;
            memcpy(&offset, data, wordsize);
            log("%s\n", dbg_line_str + offset);
            data += wordsize;
            break;
        case DW_FORM_udata:
            // TODO 应该是 LEB-128 类型，是变长的
            log("%d\n", *data);
            ++data;
            break;
        case DW_FORM_data16:
            log("\n");
            data += 16;
            break;
        default:
            log("    unsupported FORM %s\n", show_format(format[2 * i + 1]));
            break;
        }
    }

    return data;
}

// 解析一个 unit，返回下一个 unit 的地址
static uint8_t *parse_debug_line_unit(uint8_t *data, const char *dbg_str, const char *dbg_line_str) {
    size_t wordsize = sizeof(uint32_t);

    size_t unit_length = 0;
    memcpy(&unit_length, data, wordsize);
    data += wordsize;

    if (0xffffffff == unit_length) {
        wordsize = sizeof(uint64_t);
        memcpy(&unit_length, data, wordsize);
        data += wordsize;
    }

    uint8_t *end = data + unit_length;

    uint16_t version;
    memcpy(&version, data, sizeof(uint16_t));
    data += sizeof(uint16_t);

    if (5 != version) {
        log("we only support dwarf v5, but got %d\n", version);
        return end;
    }

    uint8_t address_size = *data++;
    uint8_t segment_selector_size = *data++;

    size_t header_length = 0;
    memcpy(&header_length, data, wordsize);
    data += wordsize;

    uint8_t *opcodes = data + header_length;

    uint8_t minimum_instruction_length = *data++;
    uint8_t maximum_operations_per_instruction = *data++;
    uint8_t default_is_stmt = *data++;
    int8_t  line_base = *(int8_t *)data++;
    uint8_t line_range = *data++;
    uint8_t opcode_base = *data++;

    // 表示每个 opcode 各有多少个参数
    uint8_t *standard_opcode_lengths = data;
    data += opcode_base - 1;

    log("=====================================================\n");
    log("debug line header length %ld\n", unit_length);
    log("version=%d, addr-size=%d, sel-size=%d\n", version, address_size, segment_selector_size);
    log("header length %ld\n", header_length);
    log("min_ins_len=%d, max_ops_per_ins=%d\n", minimum_instruction_length, maximum_operations_per_instruction);
    log("default is_stmt=%d\n", default_is_stmt);
    log("line_base=%d, line_range=%d, op_base=%d\n", line_base, line_range, opcode_base);

    // for (int i = 0; i + 1 < opcode_base; ++i) {
    //     log(" -> standard opcode[%d] len %d\n", i, standard_opcode_lengths[i]);
    // }

    uint8_t directory_entry_format_count = *data++;
    uint8_t *directory_formats = data;
    // for (int i = 0; i < directory_entry_format_count; ++i) {
    //     log(" #> directory field %s: %s\n", show_type_code(data[0]), show_format(data[1]));
    //     data += 2;
    // }
    data += directory_entry_format_count * 2;

    uint8_t directories_count = *data++;
    for (int i = 0; i < directories_count; ++i) {
        log("Directory #%d:\n", i);
        data = parse_directory_entry(data, directory_formats, directory_entry_format_count, wordsize, dbg_str, dbg_line_str);
    }

    uint8_t file_name_entry_format_count = *data++;
    uint8_t *file_name_formats = data;
    data += file_name_entry_format_count * 2;

    uint8_t file_name_count = *data++;
    for (int i = 0; i < file_name_count; ++i) {
        log("File #%d:\n", i);
        data = parse_directory_entry(data, file_name_formats, file_name_entry_format_count, wordsize, dbg_str, dbg_line_str);
    }

    log("---------------------------------\n");
    return end;
}

void parse_debug_line(uint8_t *data, size_t size, const char *dbg_str, const char *dbg_line_str) {
    uint8_t *end = data + size;
    while (data < end) {
        data = parse_debug_line_unit(data, dbg_str, dbg_line_str);
    }
}
