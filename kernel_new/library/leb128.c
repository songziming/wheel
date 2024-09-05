#include "leb128.h"


// LEB128 变长整型数据编解码，主要用于 DWARF
// 每个字节只有 7-bit 有效数字，最高位表示该字节是否为最后一个字节


int encode_uleb128(unsigned value, uint8_t *dst) {
    int len = 0;

    do {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        if (0 != value) {
            byte |= 0x80;
        }
        if (NULL != dst) {
            dst[len] = byte;
        }
        ++len;
    } while (0 != value);

    return len;
}

int encode_sleb128(int value, uint8_t *dst) {
    int len = 0;
    int more = 1;

    while (more) {
        uint8_t byte = value & 0x7f;
        value >>= 7;
        if (((0 == value) && !(byte & 0x40)) ||
            ((-1 == value) && (byte & 0x40))) {
            more = 0;
        } else {
            byte |= 0x80;
        }
        if (NULL != dst) {
            dst[len] = byte;
        }
        ++len;
    }

    return len;
}

unsigned decode_uleb128(const uint8_t *src, const uint8_t *end, const uint8_t **after) {
    unsigned value = 0;
    int shift = 0;

    while (src < end) {
        uint8_t byte = *src++;
        value |= (unsigned)(byte & 0x7f) << shift;
        shift += 7;
        if (0 == (byte & 0x80)) {
            goto out;
        }
    }
    value = 0;
out:
    if (NULL != after) {
        *after = src;
    }
    return value;
}

int decode_sleb128(const uint8_t *src, const uint8_t *end, const uint8_t **after) {
    int value = 0;
    int shift = 0;

    while (src < end) {
        uint8_t byte = *src++;
        value |= (int)(byte & 0x7f) << shift;
        shift += 7;
        if (0 == (byte & 0x80)) {
            if ((shift < sizeof(value) * 8) && (byte & 0x40)) {
                value |= -(1 << shift); // 如果是有符号数，需要符号扩展
            }
            goto out;
        }
    }
    value = 0;
out:
    if (NULL != after) {
        *after = src;
    }
    return value;
}
