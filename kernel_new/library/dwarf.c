#include "dwarf.h"
#include "debug.h"
#include "string.h"
#include "dllist.h"
#include "leb128.h"
#include <memory/early_alloc.h>


//------------------------------------------------------------------------------
// addr2line 映射
//------------------------------------------------------------------------------

// 我们自己的 linemap 也使用 LEB128 保存数据
// 格式：
//  - file:         const char *
//  - nbytes:       int,    表示剩余部分的长度
//  - start_addr:   SLEB128,    注意用的是有符号类型，因为内核代码一般在地址空间末尾
//  - addr_length:  ULEB128
//  - start_line:   ULEB128
//  - 接下来是若干 pair，每个 pair 由两个 leb128 组成
//      - addr_delta:   ULEB128
//      - line_delta:   SLEB128,    可能为负
typedef struct linemap {
    dlnode_t    dl;
    const char *file;
    int         nbytes;
    uint8_t     opcodes[0];
} linemap_t;

// 指令行号查找表，按地址范围组成链表
static dlnode_t g_map_head = {
    .prev = &g_map_head,
    .next = &g_map_head
};


static INIT_TEXT linemap_t *linemap_by_name(const char *name) {
    for (dlnode_t *node = g_map_head.next; node != &g_map_head; node = node->next) {
        linemap_t *map = containerof(node, linemap_t, dl);
        if (0 == strcmp(map->file, name)) {
            return map;
        }
    }
    return NULL;
}

// 打印 stacktrace 时调用此函数，作用相当于命令 addr2line
// TODO 可以通过参数返回文件名和行号
int addr_to_line(size_t target, const char **file) {
    for (dlnode_t *node = g_map_head.next; node != &g_map_head; node = node->next) {
        linemap_t *map = containerof(node, linemap_t, dl);
        const uint8_t *ptr = map->opcodes;
        const uint8_t *end = ptr + map->nbytes;
        uint64_t addr = (uint64_t)decode_sleb128(ptr, end, &ptr);
        uint64_t end_addr = addr + decode_uleb128(ptr, end, &ptr);

        if ((target < addr) || (target >= end_addr)) {
            continue;
        }

        uint64_t line = decode_uleb128(ptr, end, &ptr);

        while (ptr < end) {
            addr += decode_uleb128(ptr, end, &ptr);
            if (addr > target) {
                break;
            }
            line += decode_sleb128(ptr, end, &ptr);
        }

        if (NULL != file) {
            *file = map->file;
        }
        return line;
    }

    if (NULL != file) {
        *file = NULL;
    }
    return 0;
}


//------------------------------------------------------------------------------
// 解析 .debug_line
//------------------------------------------------------------------------------

// dwarf_line 分为多个 unit，每个 unit 开头都是 program header
// 从 program header 解析出来如下信息
typedef struct line_number_unit {
    size_t  wordsize;   // 当前 unit 使用 32-bit 还是 64-bit
    uint8_t address_size;

    int8_t  line_base;
    uint8_t line_range;
    uint8_t opcode_base;
    uint8_t min_ins_len; // 指令长度（最大公约数）
    uint8_t ops_per_ins; // 指令包含的 operation 数量

    const uint8_t *nargs; // 长度 opcode_base - 1
    const char **filenames;
} line_number_unit_t;

// addr2line 矩阵中的一行
typedef struct row {
    uint64_t    addr;
    unsigned    opix;
    int         line;
} row_t;

// 代表一段连续指令，映射相同的文件
typedef struct sequence {
    uint64_t    start_addr;
    uint64_t    addr_size; // 占据的长度
    const char *file;
    int         start_line;

    row_t       prev;
    row_t       curr;
    int         row_count;  // addr2line 矩阵已经有了多少行

    // 尝试多种数据压缩存储方式，计算每一种存储方式占用的内存
    // 第二次解析 opcodes 时，选择最节省空间的方式存储
    int         bytes_in_leb128;

    uint8_t    *dst;
} sequence_t;

// line number information state machine registers
typedef struct line_number_state {
    const dwarf_line_t  *line;
    const uint8_t       *ptr; // 目前读取到了哪个字节
    const uint8_t       *unit_end;
    line_number_unit_t   unit;
} line_number_state_t;


static INIT_TEXT size_t decode_size(line_number_state_t *state) {
    size_t addr = 0;
    memcpy(&addr, state->ptr, state->unit.wordsize);
    state->ptr += state->unit.wordsize;
    return addr;
}

static INIT_TEXT uint16_t decode_uhalf(line_number_state_t *state) {
    uint16_t value;
    memcpy(&value, state->ptr, sizeof(uint16_t));
    state->ptr += sizeof(uint16_t);
    return value;
}

static INIT_TEXT size_t decode_initial_length(line_number_state_t *state) {
    state->unit.wordsize = sizeof(uint32_t);

    size_t length = 0;
    memcpy(&length, state->ptr, state->unit.wordsize);
    state->ptr += state->unit.wordsize;

    if (0xffffffff == length) {
        state->unit.wordsize = sizeof(uint64_t);
        memcpy(&length, state->ptr, state->unit.wordsize);
        state->ptr += state->unit.wordsize;
    }

    return length;
}

// 解析一个字段，只返回字符串类型的
static INIT_TEXT const char *parse_field_str(line_number_state_t *state, dwarf_form_t form) {
    const char *s = NULL;
    switch (form) {
    case DW_FORM_string:
        s = (const char *)state->ptr;
        state->ptr += strlen(s);
        break;
    case DW_FORM_strp: {
        size_t pos = decode_size(state);
        if (pos < state->line->str_size) {
            s = state->line->str + pos;
        }
        break;
    }
    case DW_FORM_line_strp: {
        size_t pos = decode_size(state);
        if (pos < state->line->line_str_size) {
            s = state->line->line_str + pos;
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
        decode_uleb128(state->ptr, state->line->line_end, &state->ptr);
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
static INIT_TEXT const char *parse_entry_path(line_number_state_t *state, const uint64_t *formats, uint8_t format_len) {
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

static INIT_TEXT void advance_pc(sequence_t *seq, line_number_state_t *state, int op_advance) {
    int new_opix = op_advance + seq->curr.opix;
    seq->curr.opix  = new_opix % state->unit.ops_per_ins;
    seq->curr.addr += new_opix / state->unit.ops_per_ins * state->unit.min_ins_len;
}

// 在 addr2line 矩阵中新增一行
// 并不真的保存一行数据，而是计算不同存储格式下的内存占用量
// 选出最合适的存储方式再真地保存 addr2line
static INIT_TEXT void add_row(sequence_t *seq) {
    if ((0 != seq->row_count) && (seq->prev.line == seq->curr.line)) {
        return;
    }

    if (0 == seq->row_count) {
        seq->start_addr = seq->curr.addr;
        seq->start_line = seq->curr.line;
        if (seq->dst) {
            ASSERT(0 != seq->addr_size);
            seq->dst += encode_sleb128((int64_t)seq->start_addr, seq->dst);
            seq->dst += encode_uleb128(seq->addr_size, seq->dst);
            seq->dst += encode_uleb128(seq->start_line, seq->dst);
        } else {
            seq->bytes_in_leb128 += encode_sleb128((int64_t)seq->start_addr, NULL);
            seq->bytes_in_leb128 += encode_uleb128(seq->start_line, NULL);
            // 还有 addr_size，此时不能确定取值
        }
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

    memcpy(&seq->prev, &seq->curr, sizeof(row_t));
    ++seq->row_count;
}

// 解析到 end-sequence 截至，创建一个新的 sequence 对象
// 可以传入不同的 writer，对同一段 opcodes 解析两次，第一次分析需要的内存大小，第二次才生成自己格式的编码
static INIT_TEXT void parse_opcodes(sequence_t *seq, line_number_state_t *state) {
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
                ASSERT(seq->start_addr < seq->curr.addr);
                ASSERT(0 != seq->row_count);
                seq->addr_size = seq->curr.addr - seq->start_addr; // 此地址是 sequence 范围之外的
                seq->bytes_in_leb128 += encode_uleb128(seq->addr_size, NULL);
                return;
            case DW_LNE_set_address:
                ASSERT(state->unit.address_size + 1 == nbytes);
                ASSERT(0 == seq->row_count); // 不能出现在 sequence 中间
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
static INIT_TEXT const uint8_t *parse_debug_line_unit(line_number_state_t *state) {
    size_t unit_length = decode_initial_length(state); // 同时更新 wordsize
    state->unit_end = state->ptr + unit_length;

    // 解析 unit program header
    uint16_t version = decode_uhalf(state);
    if (5 != version) {
        log("warning: dwarf v%d not supported\n", version);
        return state->unit_end;
    }

    state->unit.address_size = *state->ptr++;
    if (sizeof(size_t) != state->unit.address_size) {
        log("warning: address size is %d\n", state->unit.address_size);
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
    while (state->ptr < state->unit_end) {
        const uint8_t *seq_start = state->ptr;

        sequence_t seq;
        memset(&seq, 0, sizeof(sequence_t));
        parse_opcodes(&seq, state);

        // 映射到地址 0，说明这段代码被 gc-sections 回收，无需保存
        if (0 == seq.start_addr) {
            continue;
        }

        // 创建 linemap，为其申请空间
        size_t map_size = sizeof(linemap_t) + seq.bytes_in_leb128;
        linemap_t *ref = linemap_by_name(seq.file); // 如果有同名的 linemap 则复用
        linemap_t *map = NULL;
        if (ref) {
            map = early_alloc_ro(map_size);
            map->file = ref->file;
        } else {
            map = early_alloc_ro(map_size + strlen(seq.file) + 1);
            map->file = (char *)map + map_size;
            memcpy((char *)map->file, seq.file, strlen(seq.file) + 1);
        }
        map->nbytes = seq.bytes_in_leb128;

        // 再次解析相同的 opcodes，这次转换为自己格式的 opcodes
        state->ptr = seq_start;
        seq.dst = map->opcodes;
        parse_opcodes(&seq, state);
        // if (seq.dst != map->opcodes + map->nbytes) {
        //     log("map end should be %x, but got %lx\n", map->opcodes + map->nbytes, seq.dst);
        // }
        dl_insert_before(&map->dl, &g_map_head);
    }

    return state->unit_end;
}

// 解析内核符号表时，根据 section 名称找到 debug_line、debug_str、debug_line_str 三个调试信息段
// 调用这个函数，解析行号信息，这样在断言失败、打印调用栈时可以显示对应的源码位置，而不只是所在函数
// 本函数在开机阶段调用一次，将行号信息保存下来
INIT_TEXT void parse_debug_line(dwarf_line_t *line) {
    dl_init_circular(&g_map_head);

    line_number_state_t state;
    state.line = line;
    state.ptr = line->line;

    while (state.ptr < line->line_end) {
        state.ptr = parse_debug_line_unit(&state);
    }
}
