#include "dwarf.h"
#include "string.h"
#include "debug.h"


// #include "dwarf_show.h"




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


// 解析一个字段，只返回字符串类型的
static const char *parse_field_str(dwarf_line_t *state, form_t form) {
    const char *s = NULL;
    switch (form) {
    case DW_FORM_string:
        s = (const char *)state->ptr;
        state->ptr += strlen(s);
        break;
    case DW_FORM_strp: {
        size_t pos = decode_size(state);
        if (pos < state->str_size) {
            s = state->str + pos;
        }
        break;
    }
    case DW_FORM_line_strp: {
        size_t pos = decode_size(state);
        if (pos < state->line_str_size) {
            s = state->line_str + pos;
        }
        break;
    }
    case DW_FORM_data1:
        state->ptr++;
        break;
    case DW_FORM_data2:
        decode_uhalf(state);
        break;
    case DW_FORM_udata:
        decode_uleb128(state);
        break;
    case DW_FORM_data16:
        state->ptr += 16;
        break;
    default:
        log("warning: unknown field type %u\n", form);
        break;
    }
    return s;
}

// 解析 directory entry 或 filename entry，只返回 path 字段
static const char *parse_entry_path(dwarf_line_t *state, const uint64_t *formats, uint8_t format_len) {
    const char *path = NULL;

    for (int i = 0; i < format_len; ++i) {
        if (DW_LNCT_path == formats[2 * i]) {
            path = parse_field_str(state, formats[2 * i + 1]);
        } else {
            parse_field_str(state, formats[2 * i + 1]);
        }
    }

    return path;
}


#if 0
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
#endif






// 状态机寄存器
typedef struct line_number_regs {
    uint64_t addr;
    unsigned opix;
    unsigned file;
    unsigned line;
} line_number_regs_t;


// 代表一段连续指令，映射相同的文件
typedef struct sequence {
    uint64_t    start_addr;
    uint64_t    end_addr; // sequence 有效范围之后的第一个字节
    const char *file;
    int         start_line;

    unsigned    prev_line;  // addr2line 矩阵前一行的代码行号
    uint64_t    prev_addr;
    int         row_count;  // addr2line 矩阵已经有了多少行
} sequence_t;


// line number information state machine registers
typedef struct line_num_state {
    // 从 program header 解析出来的信息
    int8_t  line_base;
    uint8_t line_range;
    uint8_t opcode_base;
    uint8_t min_ins_len; // 指令长度（最大公约数）
    uint8_t ops_per_ins; // 指令包含的 operation 数量

    const char **filenames;

    line_number_regs_t regs;    // 状态机寄存器
    sequence_t      seq; // 目前正在构建的序列
} line_num_state_t;


// // 识别出来的 addr2line 数据，使用自己的格式保存
// typedef struct addr2line {
//     //
// } addr2line_t;



// 这里 dwarf 5 标准可能写错了，file 初值应该是 0，column 初值应是 1
// 但既然文档这么写，其他工具应该也是按这个行为开发的
static void regs_init(line_number_regs_t *regs) {
    memset(regs, 0, sizeof(line_number_regs_t));
    regs->file = 1;
    regs->line = 1;
}

static void advance_pc(line_num_state_t *state, int op_advance) {
    int new_opix = op_advance + state->regs.opix;
    state->regs.opix  = new_opix % state->ops_per_ins;
    state->regs.addr += new_opix / state->ops_per_ins * state->min_ins_len;
}

// 在 addr2line 矩阵中新增一行，可以在这里检查目标地址
// 需要把前一行的内容保存下来，使用相邻两行才能检查范围
static void add_row(line_num_state_t *state) {

    // 我们不关心列号、代码类型等信息，如果行号未发生变化，
    if ((0 != state->seq.row_count) && (state->seq.prev_line == state->regs.line)) {
        return;
    }

    // TODO 第一行不用输出，直接记录在了 sequence header 中
    //      我们只需输出两行之间的 delta
    log("  > addr 0x%016lx --> %d:%d\n", state->regs.addr, state->regs.file, state->regs.line);

    state->seq.prev_line = state->regs.line;

    // 如果这是第一行，则这一行的取值就是当前 sequence 的取值
    if (0 == state->seq.row_count) {
        state->seq.start_addr = state->regs.addr;
        state->seq.start_line = state->regs.line;
        state->seq.file = state->filenames[state->regs.file];
    }
    // else {
    //     int line_delta = state->regs.line - state->seq.prev_line;
    //     int addr_delta = state->regs.addr - state->seq.prev_addr;
    // }
    ++state->seq.row_count;
    // state->seq.prev_line = state->regs.line;
    // state->seq.prev_addr = state->regs.addr;
}




// 解析一个 unit，返回下一个 unit 的地址
// uint8_t *data, const char *str, const char *line_str
static const uint8_t *parse_debug_line_unit(dwarf_line_t *dwarf) {
    // const uint8_t *unit_start = dwarf->ptr;
    size_t unit_length = decode_initial_length(dwarf); // 同时更新 wordsize
    const uint8_t *end = dwarf->ptr + unit_length;

    line_num_state_t state;
    regs_init(&state.regs);

    uint16_t version = decode_uhalf(dwarf);
    if (5 != version) {
        log("we only support dwarf v5, but got %d\n", version);
        return end;
    }

    uint8_t address_size = *dwarf->ptr++;
    UNUSED uint8_t segment_selector_size = *dwarf->ptr++;
    if (sizeof(size_t) != address_size) {
        log("address size is %d\n", address_size);
    }

    size_t header_length = decode_size(dwarf);
    const uint8_t *opcodes = dwarf->ptr + header_length;

    state.min_ins_len = *dwarf->ptr++;
    state.ops_per_ins = *dwarf->ptr++;
    UNUSED uint8_t default_is_stmt = *dwarf->ptr++;
    state.line_base = *(int8_t *)dwarf->ptr++;
    state.line_range = *dwarf->ptr++;
    state.opcode_base = *dwarf->ptr++;

    // 表示每个标准 opcode 有多少个 LEB128 参数
    const uint8_t *standard_opcode_lengths = dwarf->ptr;
    dwarf->ptr += state.opcode_base - 1;

    uint8_t dir_format_len = *dwarf->ptr++;
    uint64_t dir_formats[dir_format_len * 2];
    for (int i = 0; i < dir_format_len * 2; ++i) {
        dir_formats[i] = decode_uleb128(dwarf);
    }

    uint64_t dir_count = decode_uleb128(dwarf);
    for (unsigned i = 0; i < dir_count; ++i) {
        parse_entry_path(dwarf, dir_formats, dir_format_len);
    }

    uint8_t file_format_len = *dwarf->ptr++;
    uint64_t file_formats[file_format_len * 2];
    for (int i = 0; i < file_format_len * 2; ++i) {
        file_formats[i] = decode_uleb128(dwarf);
    }

    uint64_t filename_count = decode_uleb128(dwarf);
    const char *filenames[filename_count];
    state.filenames = filenames; // TODO 可以换成 alloca
    for (unsigned i = 0; i < filename_count; ++i) {
        filenames[i] = parse_entry_path(dwarf, file_formats, file_format_len);
    }

    // 接下来应该是指令
    if (opcodes != dwarf->ptr) {
        log(">> current pointer %p, opcode %p\n", dwarf->ptr, opcodes);
    }

    // 解析执行指令，更新状态机
    state.seq.row_count = 0;
    while (dwarf->ptr < end) {
        int op = *dwarf->ptr++;

        // 特殊指令，没有参数
        if (op >= state.opcode_base) {
            op -= state.opcode_base;
            state.regs.line += op % state.line_range + state.line_base;
            advance_pc(&state, op / state.line_range);
            add_row(&state);
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
                state.seq.end_addr = state.regs.addr;
                log("  + sequence 0x%lx-0x%lx, %s\n",
                    state.seq.start_addr, state.regs.addr, state.seq.file);
                regs_init(&state.regs);
                state.seq.row_count = 0;
                // 准备创建新的 sequence
                break;
            case DW_LNE_set_address:
                ASSERT(address_size + 1 == nbytes);
                ASSERT(0 == state.seq.row_count);   // 此命令不能出现在 sequence 中间
                state.regs.addr = 0;
                memcpy(&state.regs.addr, eop + 1, nbytes - 1);
                // log("  ~ extended set address to 0x%lx\n", state.regs.addr);
                break;
            default:
                // log("  ~ extended %s\n", show_ext_opcode(eop[0]));
                break;
            }
            continue;
        }

        // 标准指令
        switch (op) {
        case DW_LNS_copy:
            add_row(&state);
            break;
        case DW_LNS_advance_pc:
            ASSERT(1 == standard_opcode_lengths[op - 1]);
            advance_pc(&state, decode_uleb128(dwarf));
            break;
        case DW_LNS_advance_line: {
            ASSERT(1 == standard_opcode_lengths[op - 1]);
            int64_t adv = decode_sleb128(dwarf);
            if ((int64_t)state.regs.line + adv < 0) {
                state.regs.line = 0;
            } else {
                state.regs.line += adv;
            }
            break;
        }
        case DW_LNS_set_file:
            ASSERT(1 == standard_opcode_lengths[op - 1]);
            ASSERT(0 == state.seq.row_count);   // 此命令不能出现在 sequence 中间
            state.regs.file = decode_uleb128(dwarf);
            log("set file to %s\n", filenames[state.regs.file]);
            break;
        case DW_LNS_const_add_pc:
            // 相当于 special opcode 255，但是不更新 line，而且不新增行
            ASSERT(0 == standard_opcode_lengths[op - 1]);
            advance_pc(&state, (255 - state.opcode_base) / state.line_range);
            break;
        case DW_LNS_fixed_advance_pc:
            // 这个指令参数不是 LEB128，而是 uhalf
            state.regs.addr += decode_uhalf(dwarf);
            state.regs.opix = 0;
            break;
        default:
            // log("standard %s\n", show_std_opcode(op));
            for (int i = 0; i < standard_opcode_lengths[op - 1]; ++i) {
                decode_uleb128(dwarf);
            }
            break;
        }
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
