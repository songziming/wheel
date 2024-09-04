#include "dwarf.h"
#include "string.h"
#include "debug.h"

// #include "dwarf_show.h"
#include "dwarf_decode.h"


static void advance_pc(sequence_t *seq, line_number_state_t *state, int op_advance) {
    int new_opix = op_advance + seq->curr.opix;
    seq->curr.opix  = new_opix % state->unit.ops_per_ins;
    seq->curr.addr += new_opix / state->unit.ops_per_ins * state->unit.min_ins_len;
}

// 在 addr2line 矩阵中新增一行，可以在这里检查目标地址
// 需要把前一行的内容保存下来，使用相邻两行才能检查范围
static void add_row(sequence_t *seq) {
    if ((0 != seq->row_count) && (seq->prev.line == seq->curr.line)) {
        return;
    }

    if (0 == seq->row_count) {
        seq->start_addr = seq->curr.addr;
        seq->start_line = seq->curr.line;
        log("  > sequence start at addr 0x%lx, line %d\n", seq->start_addr, seq->start_line);
    } else {
        uint64_t addr_inc = seq->curr.addr - seq->prev.addr;
        int      line_inc = seq->curr.line - seq->prev.line; // 可能为负
        seq->bytes_in_leb128 += encode_uleb128(addr_inc, NULL);
        seq->bytes_in_leb128 += encode_sleb128(line_inc, NULL);
        log("  > addr += 0x%lx, line += %d\n", addr_inc, line_inc);
    }

    memcpy(&seq->prev, &seq->curr, sizeof(sequence_row_t));
    ++seq->row_count;
}



// 解析到 end-sequence 截至，创建一个新的 sequence 对象
// 可以传入不同的 writer，对同一段 opcodes 解析两次，第一次分析需要的内存大小，第二次才生成自己格式的编码
static void parse_opcodes(sequence_t *seq, line_number_state_t *state, const uint8_t *unit_end) {
    seq->row_count = 0;
    seq->curr.addr = 0;
    seq->curr.line = 1;
    seq->file = state->unit.filenames[1];

    while (state->ptr < unit_end) {
        int op = *state->ptr++;

        // 特殊指令，没有参数
        if (op >= state->unit.opcode_base) {
            op -= state->unit.opcode_base;
            seq->curr.line += op % state->unit.line_range + state->unit.line_base;
            // state->regs.line += op % state->unit.line_range + state->unit.line_base;
            advance_pc(seq, state, op / state->unit.line_range);
            add_row(seq);
            continue;
        }

        // 扩展指令
        if (0 == op) {
            uint64_t nbytes = decode_uleb128(state);
            const uint8_t *eop = state->ptr;
            state->ptr += nbytes;
            switch (eop[0]) {
            case DW_LNE_end_sequence:
                ASSERT(1 == nbytes);
                seq->end_addr = seq->curr.addr;
                // log("  + sequence 0x%lx-0x%lx, %s\n",
                //     seq->start_addr, state->regs.addr, seq->file);
                log("  + end-of-sequence, %d rows\n", seq->row_count);
                add_row(seq);
                return;
                // regs_init(&state->regs);
                // seq->row_count = 0;
                // // 准备创建新的 sequence
                break;
            case DW_LNE_set_address:
                ASSERT(state->unit.address_size + 1 == nbytes);
                ASSERT(0 == seq->row_count); // 此命令不能出现在 sequence 中间
                seq->curr.addr = 0;
                memcpy(&seq->curr.addr, eop + 1, nbytes - 1);
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
            add_row(seq);
            break;
        case DW_LNS_advance_pc:
            ASSERT(1 == state->unit.nargs[op - 1]);
            advance_pc(seq, state, decode_uleb128(state));
            break;
        case DW_LNS_advance_line: {
            ASSERT(1 == state->unit.nargs[op - 1]);
            int64_t adv = decode_sleb128(state);
            if ((int64_t)seq->curr.line + adv < 0) {
                seq->curr.line = 0;
            } else {
                seq->curr.line += adv;
            }
            break;
        }
        case DW_LNS_set_file:
            ASSERT(1 == state->unit.nargs[op - 1]);
            ASSERT(0 == seq->row_count);   // 此命令不能出现在 sequence 中间
            seq->file = state->unit.filenames[decode_uleb128(state)];
            log("set file to %s\n", seq->file);
            break;
        case DW_LNS_const_add_pc:
            // 相当于 special opcode 255，但是不更新 line，而且不新增行
            ASSERT(0 == state->unit.nargs[op - 1]);
            advance_pc(seq, state, (255 - state->unit.opcode_base) / state->unit.line_range);
            break;
        case DW_LNS_fixed_advance_pc:
            // 这个指令参数不是 LEB128，而是 uhalf
            seq->curr.addr += decode_uhalf(state);
            seq->curr.opix = 0;
            break;
        default:
            // log("standard %s\n", show_std_opcode(op));
            for (int i = 0; i < state->unit.nargs[op - 1]; ++i) {
                decode_uleb128(state);
            }
            break;
        }
    }
}




// 解析一个 unit，返回下一个 unit 的地址，每个 unit 可能包含多个 sequence
// uint8_t *data, const char *str, const char *line_str
static const uint8_t *parse_debug_line_unit(line_number_state_t *state) {
    // const uint8_t *unit_start = dwarf->ptr;
    size_t unit_length = decode_initial_length(state); // 同时更新 wordsize
    const uint8_t *unit_end = state->ptr + unit_length;


    // 解析 unit program header

    uint16_t version = decode_uhalf(state);
    if (5 != version) {
        log("we only support dwarf v5, but got %d\n", version);
        return unit_end;
    }

    state->unit.address_size = *state->ptr++;
    if (sizeof(size_t) != state->unit.address_size) {
        log("address size is %d\n", state->unit.address_size);
    }

    UNUSED uint8_t segment_selector_size = *state->ptr++;

    size_t header_length = decode_size(state);
    const uint8_t *opcodes = state->ptr + header_length;

    state->unit.min_ins_len = *state->ptr++;
    state->unit.ops_per_ins = *state->ptr++;
    UNUSED uint8_t default_is_stmt = *state->ptr++;
    state->unit.line_base = *(int8_t *)state->ptr++;
    state->unit.line_range = *state->ptr++;
    state->unit.opcode_base = *state->ptr++;

    // 表示每个标准 opcode 有多少个 LEB128 参数
    state->unit.nargs = state->ptr;
    state->ptr += state->unit.opcode_base - 1;

    uint8_t dir_format_len = *state->ptr++;
    uint64_t dir_formats[dir_format_len * 2];
    for (int i = 0; i < dir_format_len * 2; ++i) {
        dir_formats[i] = decode_uleb128(state);
    }

    uint64_t dir_count = decode_uleb128(state);
    for (unsigned i = 0; i < dir_count; ++i) {
        parse_entry_path(state, dir_formats, dir_format_len);
    }

    uint8_t file_format_len = *state->ptr++;
    uint64_t file_formats[file_format_len * 2];
    for (int i = 0; i < file_format_len * 2; ++i) {
        file_formats[i] = decode_uleb128(state);
    }

    uint64_t filename_count = decode_uleb128(state);
    const char *filenames[filename_count];
    state->unit.filenames = filenames; // TODO 可以换成 alloca
    for (unsigned i = 0; i < filename_count; ++i) {
        filenames[i] = parse_entry_path(state, file_formats, file_format_len);
    }

    // 接下来应该是指令，如果不是，说明解析过程有误
    if (opcodes != state->ptr) {
        log(">> current pointer %p, opcode %p\n", state->ptr, opcodes);
    }

    // 执行 opcodes
    // TODO 每次调用创建一个 sequence，多次调用，直到当前 unit 解析结束
    // regs_init(&state->regs);
    sequence_t seq;
    memset(&seq, 0, sizeof(sequence_t));

    while (state->ptr < unit_end) {
        const uint8_t *seq_start = state->ptr;
        // regs_init(&state->regs);
        // seq->row_count = 0;
        parse_opcodes(&seq, state, unit_end);
        // state->ptr = seq_start;
        // parse_opcodes(state, unit_end);
    }

    return unit_end;
}

// 解析内核符号表时，根据 section 名称找到 debug_line、debug_str、debug_line_str 三个调试信息段
// 调用这个函数，解析行号信息，这样在断言失败、打印调用栈时可以显示对应的源码位置，而不只是所在函数
// 本函数在开机阶段调用一次，将行号信息保存下来
void parse_debug_line(dwarf_line_t *line) {
    line_number_state_t state;
    state.line = line;
    state.ptr = line->line;

    // const char *hr = "====================";

    // for (int i = 0; state.ptr < line->line_end; ++i) {
    while (state.ptr < line->line_end) {
        // log("%s%s%s%s\n", hr, hr, hr, hr);
        // log("Debug Line Unit #%d:\n", i);
        state.ptr = parse_debug_line_unit(&state);
    }
    // log("%s%s%s%s\n", hr, hr, hr, hr);
}

// 寻找指定内存地址对应的源码位置
void show_line_info() {
    // TODO
}
