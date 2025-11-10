#ifndef LEB128_H
#define LEB128_H

#include <common.h>

int encode_uleb128(uint64_t value, uint8_t *dst);
int encode_sleb128(int64_t value, uint8_t *dst);

uint64_t decode_uleb128(const uint8_t *src, const uint8_t *end, const uint8_t **after);
int64_t decode_sleb128(const uint8_t *src, const uint8_t *end, const uint8_t **after);

#endif // LEB128_H
