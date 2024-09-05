#include "dwarf.h"
#include "string.h"
#include "leb128.h"

size_t decode_size(line_number_state_t *state) {
    size_t addr = 0;
    memcpy(&addr, state->ptr, state->unit.wordsize);
    state->ptr += state->unit.wordsize;
    return addr;
}

uint16_t decode_uhalf(line_number_state_t *state) {
    uint16_t value;
    memcpy(&value, state->ptr, sizeof(uint16_t));
    state->ptr += sizeof(uint16_t);
    return value;
}

size_t decode_initial_length(line_number_state_t *state) {
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
const char *parse_field_str(line_number_state_t *state, dwarf_form_t form) {
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
const char *parse_entry_path(line_number_state_t *state, const uint64_t *formats, uint8_t format_len) {
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
