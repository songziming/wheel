#include "dwarf.h"
#include "string.h"

// DWARF 文件里，整型字段使用 LEB128 变长编码
// 每个字节只有 7-bit 有效数字，最高位表示该字节是否为最后一个字节

static uint64_t decode_uleb128(line_number_state_t *state) {
    uint64_t value = 0;
    int shift = 0;

    while (state->ptr < state->line->line_end) {
        uint8_t byte = *state->ptr++;
        value |= (uint64_t)(byte & 0x7f) << shift;
        shift += 7;
        if (0 == (byte & 0x80)) {
            return value;
        }
    }

    return 0;
}

static int64_t decode_sleb128(line_number_state_t *state) {
    int64_t value = 0;
    int shift = 0;

    while (state->ptr < state->line->line_end) {
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

    return 0;
}

static size_t decode_size(line_number_state_t *state) {
    size_t addr = 0;
    memcpy(&addr, state->ptr, state->unit.wordsize);
    state->ptr += state->unit.wordsize;
    return addr;
}

static uint16_t decode_uhalf(line_number_state_t *state) {
    uint16_t value;
    memcpy(&value, state->ptr, sizeof(uint16_t));
    state->ptr += sizeof(uint16_t);
    return value;
}

static size_t decode_initial_length(line_number_state_t *state) {
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
static const char *parse_field_str(line_number_state_t *state, dwarf_form_t form) {
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
static const char *parse_entry_path(line_number_state_t *state, const uint64_t *formats, uint8_t format_len) {
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
