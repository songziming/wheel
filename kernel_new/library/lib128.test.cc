#include <gtest/gtest.h>

extern "C" {
#include "leb128.h"
}


TEST(DwarfLeb128, Unsigned) {
    uint8_t buf[64];
    const uint8_t *end;
    int len = encode_uleb128(12345, buf);
    int val = decode_uleb128(buf, buf + sizeof(buf), &end);
    EXPECT_EQ(end, buf + len);
    EXPECT_EQ(val, 12345);

    len = encode_uleb128(624485, buf);
    EXPECT_EQ(3, len);
    EXPECT_EQ(0xe5, buf[0]);
    EXPECT_EQ(0x8e, buf[1]);
    EXPECT_EQ(0x26, buf[2]);
    val = decode_uleb128(buf, buf + sizeof(buf), &end);
    EXPECT_EQ(end, buf + len);
    EXPECT_EQ(val, 624485);
}

TEST(DwarfLeb128, Signed) {
    uint8_t buf[64];
    const uint8_t *end;
    int len = encode_sleb128(12345, buf);
    int val = decode_sleb128(buf, buf + sizeof(buf), &end);
    EXPECT_EQ(end, buf + len);
    EXPECT_EQ(val, 12345);

    len = encode_sleb128(-123456, buf);
    EXPECT_EQ(3, len);
    EXPECT_EQ(0xc0, buf[0]);
    EXPECT_EQ(0xbb, buf[1]);
    EXPECT_EQ(0x78, buf[2]);
    val = decode_sleb128(buf, buf + sizeof(buf), &end);
    EXPECT_EQ(end, buf + len);
    EXPECT_EQ(val, -123456);
}
