#include "dwarf.h"
#include "string.h"
#include "debug.h"


#include "dwarf_show.h"




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




// 状态机寄存器
typedef struct line_number_regs {
    uint64_t addr;
    unsigned opix;
    unsigned file;
    unsigned line;
} line_number_regs_t;


// line number information state machine registers
typedef struct line_num_state {
    // 从 program header 解析出来的信息
    int8_t  line_base;
    uint8_t line_range;
    uint8_t opcode_base;
    uint8_t min_ins_len; // 指令长度（最大公约数）
    uint8_t ops_per_ins; // 指令包含的 operation 数量

    // 状态机寄存器
    struct {
        uint64_t addr;
        unsigned opix;
        unsigned file;
        unsigned line;
    } prev, next;
} line_num_state_t;

static void line_number_state_init(line_num_state_t *state) {
    memset(&state->prev, 0, sizeof(state->prev));
    state->prev.file = 1;
    state->prev.line = 1;
    memset(&state->next, 0, sizeof(state->next));
    state->next.file = 1;
    state->next.line = 1;
}

// 这里 dwarf 5 标准可能写错了，file 初值应该是 0，column 初值应是 1
// 但既然文档这么写，其他工具应该也是按这个行为开发的
static void regs_init(line_number_regs_t *regs) {
    memset(regs, 0, sizeof(line_number_regs_t));
    regs->file = 1;
    regs->line = 1;
}

static void advance_pc(line_num_state_t *state, int op_advance) {
    int new_opix = op_advance + state->next.opix;
    state->next.opix  = new_opix % state->ops_per_ins;
    state->next.addr += new_opix / state->ops_per_ins * state->min_ins_len;
}

// 在 addr2line 矩阵中新增一行，可以在这里检查目标地址
// 需要把前一行的内容保存下来，使用相邻两行才能检查范围
static void add_row(line_num_state_t *state, uint64_t target) {
    if (state->prev.addr <= target && target < state->next.addr) {
        log("addr 0x%lx map to %s:%d\n", target, "file", state->prev.line);
    }
    state->prev = state->next;
    log("  > addr 0x%016lx --> %d:%d\n", state->prev.addr, state->prev.file, state->prev.line);
}

// 解析一个 unit，返回下一个 unit 的地址
// uint8_t *data, const char *str, const char *line_str
static const uint8_t *parse_debug_line_unit(dwarf_line_t *dwarf) {
    // const uint8_t *unit_start = dwarf->ptr;
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

    state.min_ins_len = *dwarf->ptr++;
    state.ops_per_ins = *dwarf->ptr++;
    uint8_t default_is_stmt = *dwarf->ptr++;
    state.line_base = *(int8_t *)dwarf->ptr++;
    state.line_range = *dwarf->ptr++;
    uint8_t opcode_base = *dwarf->ptr++;

    // state.is_stmt = default_is_stmt;

    // 表示每个 opcode 各有多少个参数
    const uint8_t *standard_opcode_lengths = dwarf->ptr;
    dwarf->ptr += opcode_base - 1;

    log("- unit length %ld\n", unit_length);
    log("- version=%d, addr-size=%d, sel-size=%d\n", version, address_size, segment_selector_size);
    log("- header length %ld\n", header_length);
    // log("- min_ins_len=%d, ops_per_ins=%d\n", min_ins_len, ops_per_ins);
    log("- default is_stmt=%d\n", default_is_stmt);
    // log("- line_base=%d, line_range=%d, op_base=%d\n", line_base, line_range, opcode_base);

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
        // size_t rela = (size_t)(dwarf->ptr - unit_start);
        // log("  [%lx] ", rela);

        int op = *dwarf->ptr++;

        // 特殊指令，没有参数
        if (op >= opcode_base) {
            op -= opcode_base;
            state.next.line += op % state.line_range + state.line_base;
            advance_pc(&state, op / state.line_range);
            add_row(&state, 0);
            continue;
        }

        // 扩展指令
        if (0 == op) {
            uint64_t nbytes = decode_uleb128(dwarf);
            const uint8_t *eop = dwarf->ptr;
            dwarf->ptr += nbytes;
            switch (eop[0]) {
            case DW_LNE_end_sequence:
                ASSERT(1 == nbytes);
                add_row(&state, 0);
                line_number_state_init(&state);
                // TODO 这里可以创建一个 sequence 记录，方便运行状态快速索引
                log("  ~ extended end sequence\n");
                break;
            case DW_LNE_set_address:
                ASSERT(sizeof(size_t) + 1 == nbytes);
                state.next.addr = 0;
                memcpy(&state.next.addr, eop + 1, nbytes - 1);
                log("  ~ extended set address to 0x%lx\n", state.next.addr);
                break;
            default:
                log("  ~ extended %s\n", show_ext_opcode(eop[0]));
                break;
            }
            continue;
        }

        // 标准指令
        switch (op) {
        case DW_LNS_copy:
            add_row(&state, 0);
            break;
        case DW_LNS_advance_pc:
            ASSERT(1 == standard_opcode_lengths[op - 1]);
            // state.addr += decode_uleb128(dwarf);
            advance_pc(&state, decode_uleb128(dwarf));
            break;
        case DW_LNS_advance_line:
            ASSERT(1 == standard_opcode_lengths[op - 1]);
            state.next.line += decode_uleb128(dwarf);
            break;
        case DW_LNS_set_file:
            ASSERT(1 == standard_opcode_lengths[op - 1]);
            state.next.file = decode_uleb128(dwarf);
            break;
        case DW_LNS_const_add_pc:
            ASSERT(0 == standard_opcode_lengths[op - 1]);
            // 相当于 special opcode 255，但是不更新 line，而且不新增行
            advance_pc(&state, (255 - opcode_base) / state.line_range);
            // add_row(&state, 0);
            break;
        case DW_LNS_fixed_advance_pc:
            // 这个指令参数不是 LEB128，而是 uhalf
            state.next.addr += decode_uhalf(dwarf);
            state.next.opix = 0;
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
