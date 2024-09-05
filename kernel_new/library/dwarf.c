#include "dwarf.h"
#include "string.h"
#include "debug.h"
#include <memory/early_alloc.h>
#include "dllist.h"

// #include "dwarf_show.h"
#include "dwarf_decode.h"
#include "leb128.h"




// 我们自己的 linemap 也使用 LEB128 保存数据
// 格式：
//  - start_addr:   ULEB128
//  - addr_length:  ULEB128
//  - start_line:   ULEB128
//  - 接下来是若干 pair，每个 pair 由两个 leb128 组成
//      - addr_delta:   ULEB128
//      - line_delta:   SLEB128,    可能为负
typedef struct linemap {
    dlnode_t    dl;
    const char *file;
    uint64_t start_addr;
    uint64_t end_addr;
    int      start_line;
    int      nbytes;
    uint8_t  opcodes[0];
} linemap_t;



// 指令行号查找表，按地址范围组成链表
static dlnode_t g_map_head;


linemap_t *linemap_by_name(const char *name) {
    for (dlnode_t *node = g_map_head.next; node != &g_map_head; node = node->next) {
        linemap_t *map = containerof(node, linemap_t, dl);
        if (0 == strcmp(map->file, name)) {
            return map;
        }
    }
    return NULL;
}

linemap_t *linemap_by_addr(uint64_t addr) {
    for (dlnode_t *node = g_map_head.next; node != &g_map_head; node = node->next) {
        linemap_t *map = containerof(node, linemap_t, dl);
        if ((map->start_addr <= addr) && (addr < map->end_addr)) {
            return map;
        }
    }
    return NULL;
}


// 打印 stacktrace 时调用此函数，作用相当于命令 addr2line
// TODO 可以通过参数返回文件名和行号
void addr_lookup(size_t target) {
    linemap_t *map = linemap_by_addr(target);
    if (NULL == map) {
        log("(no source)");
        return;
    }

    uint64_t addr = map->start_addr;
    int      line = map->start_line;

    const uint8_t *ptr = map->opcodes;
    const uint8_t *end = ptr + map->nbytes;

    while (ptr < end) {
        addr += decode_uleb128(ptr, end, &ptr);
        if (addr > target) {
            break;
        }
        line += decode_sleb128(ptr, end, &ptr);
    }

    log("0x%lx==>%s:%d", target, map->file, line);
}



static void advance_pc(sequence_t *seq, line_number_state_t *state, int op_advance) {
    int new_opix = op_advance + seq->curr.opix;
    seq->curr.opix  = new_opix % state->unit.ops_per_ins;
    seq->curr.addr += new_opix / state->unit.ops_per_ins * state->unit.min_ins_len;
}

// 在 addr2line 矩阵中新增一行
// 并不真的保存一行数据，而是计算不同存储格式下的内存占用量
// 选出最合适的存储方式再真地保存 addr2line
static void add_row(sequence_t *seq) {
    if ((0 != seq->row_count) && (seq->prev.line == seq->curr.line)) {
        return;
    }

    if (0 == seq->row_count) {
        seq->start_addr = seq->curr.addr;
        seq->start_line = seq->curr.line;
        // log("  > sequence start at addr 0x%lx, line %d\n", seq->start_addr, seq->start_line);
    } else {
        uint64_t addr_inc = seq->curr.addr - seq->prev.addr;
        int      line_inc = seq->curr.line - seq->prev.line; // 可能为负
        if (seq->dst) {
            seq->dst += encode_uleb128(addr_inc, seq->dst);
            seq->dst += encode_sleb128(line_inc, seq->dst);
        } else {
            seq->bytes_in_leb128 += encode_uleb128(addr_inc, NULL);
            seq->bytes_in_leb128 += encode_sleb128(line_inc, NULL);
        }
    }

    memcpy(&seq->prev, &seq->curr, sizeof(sequence_row_t));
    ++seq->row_count;
}






// 解析到 end-sequence 截至，创建一个新的 sequence 对象
// 可以传入不同的 writer，对同一段 opcodes 解析两次，第一次分析需要的内存大小，第二次才生成自己格式的编码
static void parse_opcodes(sequence_t *seq, line_number_state_t *state) {
    seq->row_count = 0;
    seq->curr.addr = 0;
    seq->curr.line = 1;
    seq->file = state->unit.filenames[1];

    while (state->ptr < state->unit_end) {
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
            uint64_t nbytes = decode_uleb128(state->ptr, state->line->line_end, &state->ptr);
            const uint8_t *eop = state->ptr;
            state->ptr += nbytes;
            switch (eop[0]) {
            case DW_LNE_end_sequence:
                ASSERT(1 == nbytes);
                seq->end_addr = seq->curr.addr; // 此地址是 sequence 范围之外的
                return;
            case DW_LNE_set_address:
                ASSERT(state->unit.address_size + 1 == nbytes);
                ASSERT(0 == seq->row_count); // 此命令不能出现在 sequence 中间
                seq->curr.addr = 0;
                memcpy(&seq->curr.addr, eop + 1, nbytes - 1);
                break;
            default:
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
            advance_pc(seq, state, decode_uleb128(state->ptr, state->line->line_end, &state->ptr));
            break;
        case DW_LNS_advance_line:
            ASSERT(1 == state->unit.nargs[op - 1]);
            seq->curr.line += decode_sleb128(state->ptr, state->line->line_end, &state->ptr);
            if (seq->curr.line < 0) {
                seq->curr.line = 0;
            }
            break;
        case DW_LNS_set_file:
            ASSERT(1 == state->unit.nargs[op - 1]);
            ASSERT(0 == seq->row_count); // 此命令不能出现在 sequence 中间
            seq->file = state->unit.filenames[decode_uleb128(state->ptr, state->line->line_end, &state->ptr)];
            // log("set file to %s\n", seq->file);
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
                decode_uleb128(state->ptr, state->line->line_end, &state->ptr);
            }
            break;
        }
    }
}




// 解析一个 unit，返回下一个 unit 的地址，每个 unit 可能包含多个 sequence
// uint8_t *data, const char *str, const char *line_str
static const uint8_t *parse_debug_line_unit(line_number_state_t *state) {
    size_t unit_length = decode_initial_length(state); // 同时更新 wordsize
    state->unit_end = state->ptr + unit_length;

    // 解析 unit program header
    uint16_t version = decode_uhalf(state);
    if (5 != version) {
        log("we only support dwarf v5, but got %d\n", version);
        return state->unit_end;
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
        dir_formats[i] = decode_uleb128(state->ptr, state->line->line_end, &state->ptr);
    }

    uint64_t dir_count = decode_uleb128(state->ptr, state->line->line_end, &state->ptr);
    for (unsigned i = 0; i < dir_count; ++i) {
        parse_entry_path(state, dir_formats, dir_format_len);
    }

    uint8_t file_format_len = *state->ptr++;
    uint64_t file_formats[file_format_len * 2];
    for (int i = 0; i < file_format_len * 2; ++i) {
        file_formats[i] = decode_uleb128(state->ptr, state->line->line_end, &state->ptr);
    }

    uint64_t filename_count = decode_uleb128(state->ptr, state->line->line_end, &state->ptr);
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

    while (state->ptr < state->unit_end) {
        const uint8_t *seq_start = state->ptr;
        // regs_init(&state->regs);
        // seq->row_count = 0;
        memset(&seq, 0, sizeof(sequence_t));
        parse_opcodes(&seq, state);
        log("sequence %s %lx-%lx, %d bytes\n", seq.file, seq.start_addr, seq.end_addr, seq.bytes_in_leb128);

        linemap_t *samename = linemap_by_name(seq.file);

        // 创建 linemap，为其申请空间
        linemap_t *map = early_alloc_ro(sizeof(linemap_t) + seq.bytes_in_leb128);
        log("allocate line map at %lx\n", map);
        if (samename) {
            log("reusing filename from old map\n");
            map->file = samename->file;
        } else {
            map->file = early_alloc_ro(strlen(seq.file) + 1);
            memcpy((char *)map->file, seq.file, strlen(seq.file) + 1);
        }
        map->start_addr = seq.start_addr;
        map->end_addr = seq.end_addr;
        map->start_line = seq.start_line;
        map->nbytes = seq.bytes_in_leb128;
        seq.dst = map->opcodes;

        // 再次解析相同的 opcodes，这次转换为自己格式的 opcodes
        state->ptr = seq_start;
        parse_opcodes(&seq, state);
        if (seq.dst != map->opcodes + map->nbytes) {
            // ASSERT(seq.dst == map->opcodes + map->nbytes);
            log("map end should be %x, but got %lx\n", map->opcodes + map->nbytes, seq.dst);
        }
        dl_insert_before(&map->dl, &g_map_head);
    }

    return state->unit_end;
}

// 解析内核符号表时，根据 section 名称找到 debug_line、debug_str、debug_line_str 三个调试信息段
// 调用这个函数，解析行号信息，这样在断言失败、打印调用栈时可以显示对应的源码位置，而不只是所在函数
// 本函数在开机阶段调用一次，将行号信息保存下来
void parse_debug_line(dwarf_line_t *line) {
    line_number_state_t state;
    state.line = line;
    state.ptr = line->line;

    dl_init_circular(&g_map_head);

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
