#include "test.h"
#include <library/leb128.h>


TEST(Leb128, Unsigned) {
    uint8_t buf[64];
    const uint8_t *end;
    int len = encode_uleb128(12345, buf);
    int val = decode_uleb128(buf, buf + sizeof(buf), &end);
    EXPECT(end == buf + len);
    EXPECT(val == 12345);

    len = encode_uleb128(624485, buf);
    EXPECT(3 == len);
    EXPECT(0xe5 == buf[0]);
    EXPECT(0x8e == buf[1]);
    EXPECT(0x26 == buf[2]);
    val = decode_uleb128(buf, buf + sizeof(buf), &end);
    EXPECT(end == buf + len);
    EXPECT(val == 624485);
}

TEST(Leb128, Signed) {
    uint8_t buf[64];
    const uint8_t *end;
    int len = encode_sleb128(12345, buf);
    int val = decode_sleb128(buf, buf + sizeof(buf), &end);
    EXPECT(end == buf + len);
    EXPECT(val == 12345);

    len = encode_sleb128(-123456, buf);
    EXPECT(3 == len);
    EXPECT(0xc0 == buf[0]);
    EXPECT(0xbb == buf[1]);
    EXPECT(0x78 == buf[2]);
    val = decode_sleb128(buf, buf + sizeof(buf), &end);
    EXPECT(end == buf + len);
    EXPECT(val == -123456);
}
