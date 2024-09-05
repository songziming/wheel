#ifndef LEB128_H
#define LEB128_H

#include <common.h>

int encode_uleb128(unsigned value, uint8_t *dst);
int encode_sleb128(int value, uint8_t *dst);

unsigned decode_uleb128(const uint8_t *src, const uint8_t *end, const uint8_t **after);
int decode_sleb128(const uint8_t *src, const uint8_t *end, const uint8_t **after);

#endif // LEB128_H
