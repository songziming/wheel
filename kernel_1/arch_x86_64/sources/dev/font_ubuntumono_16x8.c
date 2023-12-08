#include <dev/framebuf.h>

static const uint8_t glyph_data[] = {
    0x00, 0x00, 0x00, 0x77, 0x4c, 0x6f, 0x4c, 0x77, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x18, 0x24, 0x7e, 0x7e, 0x3c, 0x28, 0x10, 0x00, 0x00, 0x00,
    0x2b, 0xbf, 0x18, 0xde, 0x93, 0xc9, 0xb1, 0x5e, 0xdf, 0xbe, 0x72, 0x5a, 0xbb, 0x42, 0x64, 0xc6,
    0xd8, 0x93, 0xb7, 0x15, 0x74, 0x1c, 0x8b, 0x64, 0x91, 0xf5, 0xde, 0x29, 0x46, 0x42, 0xec, 0x6f,
    0xca, 0x20, 0x15, 0xf0, 0x06, 0x27, 0x61, 0x27, 0x87, 0xe0, 0x6e, 0x43, 0x50, 0xc5, 0x1b, 0xc5,
    0xb4, 0x37, 0xc3, 0x69, 0xa6, 0xee, 0x80, 0xaf, 0x6f, 0x9b, 0x93, 0xa1, 0x76, 0xa1, 0x23, 0xf5,
    0x24, 0x72, 0x53, 0xf3, 0x5b, 0x65, 0x19, 0xf4, 0xfc, 0x93, 0xdd, 0x26, 0xe8, 0xa6, 0x10, 0xf4,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x3c, 0x3c, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf7, 0xc9, 0xce, 0x92, 0x48, 0xf6, 0x94, 0x6f, 0x60, 0xec, 0x07, 0xc4, 0xb9, 0x97, 0x6d, 0xa4,
    0xbf, 0x11, 0x0d, 0xc6, 0xb4, 0x1f, 0x4d, 0x13, 0xb0, 0x5d, 0xba, 0x31, 0x27, 0x29, 0xd5, 0x8d,
    0x51, 0x87, 0x6e, 0x36, 0xba, 0x00, 0x96, 0x7a, 0xf0, 0xc3, 0x20, 0x03, 0x7f, 0xd8, 0xda, 0x17,
    0xdb, 0xc9, 0x94, 0x19, 0xd4, 0xbf, 0xe8, 0x83, 0xe2, 0xf6, 0x91, 0x79, 0x6a, 0xa6, 0xe1, 0x95,
    0x38, 0xff, 0x28, 0xb2, 0xb3, 0xfc, 0xa6, 0xa7, 0xd8, 0xae, 0xf8, 0x54, 0xcc, 0x28, 0xdc, 0x9a,
    0x6b, 0xfb, 0xde, 0x76, 0x3f, 0xd8, 0xd7, 0xbc, 0x21, 0x7a, 0xc8, 0x7f, 0x91, 0x71, 0x09, 0x54,
    0x6d, 0x95, 0x16, 0xac, 0x96, 0x3c, 0xbe, 0xf5, 0xdd, 0x13, 0x8a, 0x62, 0x00, 0xb7, 0x0d, 0x05,
    0xc2, 0xa1, 0xee, 0x8c, 0x69, 0x64, 0x32, 0x4e, 0x35, 0x9c, 0x5f, 0x29, 0x75, 0xcd, 0x2e, 0xb7,
    0x7a, 0x24, 0xa2, 0x3e, 0x1f, 0xdf, 0x1a, 0xc1, 0x61, 0x8e, 0x14, 0x60, 0xa0, 0xca, 0x6b, 0xd3,
    0x8d, 0xba, 0x7d, 0x43, 0x7f, 0x7d, 0x7d, 0xd9, 0xeb, 0x5c, 0x8b, 0x9a, 0x9c, 0x70, 0xb4, 0x37,
    0xc4, 0x4e, 0xc9, 0x16, 0xe6, 0xee, 0xf7, 0x9e, 0x11, 0x1c, 0xa1, 0x12, 0x8e, 0x3b, 0x9c, 0xef,
    0x92, 0x93, 0x76, 0x1e, 0x3c, 0xff, 0x8e, 0xdd, 0x49, 0xba, 0xd2, 0x7d, 0x97, 0x1c, 0x28, 0xb2,
    0x00, 0x00, 0x00, 0x3e, 0x7a, 0xfa, 0xfa, 0x7a, 0x3a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
    0x00, 0x00, 0x00, 0x3c, 0x20, 0x20, 0x30, 0x2c, 0x42, 0x62, 0x34, 0x0c, 0x06, 0x06, 0x7c, 0x00,
    0x98, 0xe2, 0x6e, 0x35, 0x2e, 0xaf, 0x37, 0x43, 0x39, 0x7e, 0x44, 0x72, 0x3c, 0x4b, 0xa6, 0xbf,
    0xb1, 0x6c, 0xc2, 0x65, 0x5f, 0xa4, 0x88, 0xe6, 0xa7, 0xee, 0x02, 0x2c, 0xa4, 0xc4, 0xad, 0xc5,
    0xc1, 0xe5, 0x37, 0xa2, 0x98, 0x9f, 0x5d, 0x12, 0x40, 0x5c, 0x7b, 0x80, 0x4a, 0x3d, 0xa9, 0xe5,
    0xab, 0x0b, 0xd6, 0x92, 0x2f, 0x06, 0x79, 0x90, 0x2a, 0x9c, 0xa4, 0x4e, 0xf4, 0xc4, 0xa2, 0x11,
    0x21, 0xc1, 0x7a, 0xb6, 0x1d, 0xa4, 0x65, 0xb4, 0xc8, 0xb4, 0x44, 0xc7, 0x22, 0x46, 0x14, 0x36,
    0x32, 0x9e, 0x9b, 0x8c, 0x91, 0x20, 0x12, 0xe2, 0x27, 0x90, 0x51, 0x55, 0x34, 0xad, 0x0a, 0x0a,
    0xd0, 0x10, 0xb5, 0x68, 0xe3, 0xf0, 0x00, 0x59, 0xf0, 0xfd, 0x5b, 0xfa, 0x06, 0x57, 0xd8, 0x70,
    0xb4, 0x92, 0x1a, 0x2e, 0x3d, 0xaf, 0xfc, 0x83, 0xe6, 0x77, 0x79, 0x68, 0xc2, 0x1d, 0x25, 0xb2,
    0x42, 0x87, 0x10, 0x26, 0x68, 0x15, 0xa3, 0x04, 0x3d, 0x94, 0x29, 0x43, 0x13, 0x25, 0x16, 0x24,
    0x77, 0xb8, 0x7a, 0x8e, 0x7f, 0x14, 0x4b, 0x4b, 0x54, 0x15, 0x19, 0x33, 0x1b, 0x2c, 0xd5, 0xd3,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x10, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x24, 0x24, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x12, 0x12, 0x36, 0xff, 0x24, 0x24, 0xff, 0x6c, 0x48, 0x48, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x18, 0x3e, 0x60, 0x40, 0x60, 0x1c, 0x06, 0x02, 0x46, 0x7c, 0x18, 0x18, 0x00,
    0x00, 0x00, 0x00, 0x62, 0x92, 0x94, 0x98, 0x68, 0x16, 0x19, 0x29, 0x49, 0x46, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x38, 0x6c, 0x44, 0x68, 0x38, 0x72, 0x5a, 0x4c, 0x4e, 0x7a, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x18, 0x18, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x04, 0x08, 0x10, 0x30, 0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x18, 0x0c, 0x04,
    0x00, 0x00, 0x00, 0x20, 0x30, 0x18, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0c, 0x08, 0x10, 0x20,
    0x00, 0x00, 0x00, 0x18, 0x6e, 0x18, 0x18, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x08, 0x30, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x06, 0x04, 0x04, 0x0c, 0x08, 0x08, 0x18, 0x18, 0x10, 0x10, 0x30, 0x20, 0x20, 0x60,
    0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x5a, 0x5a, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x38, 0x68, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x46, 0x06, 0x06, 0x0c, 0x08, 0x10, 0x20, 0x60, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7c, 0x04, 0x06, 0x04, 0x18, 0x06, 0x02, 0x02, 0x06, 0x7c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x04, 0x0c, 0x14, 0x24, 0x64, 0x44, 0x7e, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3e, 0x20, 0x20, 0x20, 0x38, 0x06, 0x02, 0x02, 0x06, 0x7c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0e, 0x30, 0x20, 0x60, 0x7c, 0x46, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7e, 0x06, 0x04, 0x0c, 0x08, 0x18, 0x18, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x66, 0x3c, 0x2c, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x62, 0x3e, 0x06, 0x04, 0x0c, 0x70, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x18, 0x08, 0x30, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0c, 0x30, 0x40, 0x30, 0x0c, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x30, 0x0c, 0x02, 0x0c, 0x30, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x04, 0x06, 0x04, 0x0c, 0x08, 0x18, 0x10, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1c, 0x22, 0x42, 0x4e, 0x5a, 0x52, 0x52, 0x5a, 0x4e, 0x60, 0x30, 0x1e, 0x00,
    0x00, 0x00, 0x00, 0x18, 0x18, 0x3c, 0x24, 0x24, 0x66, 0x7e, 0x42, 0x42, 0xc3, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7c, 0x46, 0x46, 0x44, 0x7c, 0x46, 0x42, 0x42, 0x46, 0x7c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1e, 0x22, 0x60, 0x40, 0x40, 0x40, 0x40, 0x60, 0x22, 0x1e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x78, 0x44, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x44, 0x78, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7e, 0x60, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0x60, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7e, 0x60, 0x60, 0x60, 0x7e, 0x60, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1e, 0x22, 0x60, 0x40, 0x40, 0x42, 0x42, 0x62, 0x22, 0x1e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3e, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x44, 0x78, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x42, 0x44, 0x4c, 0x58, 0x70, 0x58, 0x4c, 0x44, 0x42, 0x43, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x5a, 0x5a, 0x5a, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x42, 0x62, 0x72, 0x52, 0x5a, 0x4a, 0x4a, 0x46, 0x46, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x42, 0xc3, 0xc3, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7c, 0x46, 0x42, 0x42, 0x46, 0x7c, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x42, 0xc3, 0xc3, 0x42, 0x42, 0x66, 0x3c, 0x18, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x7c, 0x46, 0x42, 0x42, 0x46, 0x7c, 0x4c, 0x44, 0x46, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x62, 0x40, 0x60, 0x38, 0x0c, 0x02, 0x02, 0x46, 0x7c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xc3, 0x42, 0x42, 0x42, 0x66, 0x24, 0x24, 0x3c, 0x18, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x5a, 0x5a, 0x5a, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x42, 0x66, 0x24, 0x18, 0x18, 0x18, 0x3c, 0x24, 0x66, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xc3, 0x42, 0x66, 0x24, 0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7e, 0x06, 0x04, 0x08, 0x18, 0x10, 0x30, 0x20, 0x60, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3c,
    0x00, 0x00, 0x60, 0x20, 0x20, 0x30, 0x10, 0x10, 0x18, 0x18, 0x08, 0x08, 0x0c, 0x04, 0x04, 0x06,
    0x00, 0x00, 0x00, 0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x3c,
    0x00, 0x00, 0x00, 0x18, 0x3c, 0x24, 0x66, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
    0x00, 0x00, 0x10, 0x18, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x06, 0x02, 0x3e, 0x42, 0x42, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x7c, 0x66, 0x42, 0x42, 0x42, 0x46, 0x7c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x60, 0x40, 0x40, 0x40, 0x60, 0x1e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x02, 0x02, 0x02, 0x3e, 0x66, 0x42, 0x42, 0x42, 0x62, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x7e, 0x40, 0x60, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0f, 0x10, 0x10, 0x10, 0x7e, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x62, 0x42, 0x42, 0x42, 0x62, 0x3e, 0x06, 0x06, 0x7c,
    0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x7c, 0x46, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x70, 0x10, 0x10, 0x10, 0x10, 0x18, 0x0e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x3c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x78,
    0x00, 0x00, 0x40, 0x40, 0x40, 0x40, 0x46, 0x4c, 0x58, 0x70, 0x48, 0x44, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x18, 0x0e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x5a, 0x5a, 0x5a, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x46, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x46, 0x42, 0x42, 0x42, 0x46, 0x7c, 0x40, 0x40, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x62, 0x42, 0x42, 0x42, 0x62, 0x3e, 0x02, 0x02, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x60, 0x60, 0x3c, 0x06, 0x42, 0x7c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x7e, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x62, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x66, 0x24, 0x3c, 0x18, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0xc3, 0x5a, 0x5a, 0x7e, 0x66, 0x66, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x24, 0x18, 0x18, 0x3c, 0x26, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x62, 0x26, 0x24, 0x34, 0x1c, 0x18, 0x08, 0x10, 0x70,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x04, 0x08, 0x18, 0x30, 0x20, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0e, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x60, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0e,
    0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x00, 0x00, 0x70, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x06, 0x08, 0x08, 0x08, 0x08, 0x08, 0x70,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x72, 0x4e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xec, 0x89, 0x78, 0xb3, 0x82, 0xc4, 0xfb, 0x76, 0xab, 0xe4, 0xda, 0xc0, 0xb7, 0x16, 0xcc, 0x5f,
    0x00, 0x00, 0x00, 0x1e, 0x22, 0x60, 0x40, 0x40, 0x40, 0x40, 0x60, 0x22, 0x1e, 0x0c, 0x04, 0x1c,
    0x00, 0x00, 0x24, 0x24, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x62, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x18, 0x10, 0x00, 0x3c, 0x66, 0x42, 0x7e, 0x40, 0x60, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x18, 0x24, 0x00, 0x3c, 0x06, 0x02, 0x3e, 0x42, 0x42, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x24, 0x24, 0x00, 0x00, 0x3c, 0x06, 0x02, 0x3e, 0x42, 0x42, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x18, 0x08, 0x00, 0x3c, 0x06, 0x02, 0x3e, 0x42, 0x42, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x18, 0x24, 0x24, 0x18, 0x00, 0x3c, 0x06, 0x02, 0x3e, 0x42, 0x42, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x60, 0x40, 0x40, 0x40, 0x60, 0x1e, 0x08, 0x08, 0x18,
    0x00, 0x00, 0x10, 0x18, 0x24, 0x00, 0x3c, 0x66, 0x42, 0x7e, 0x40, 0x60, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x24, 0x24, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x7e, 0x40, 0x60, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x18, 0x08, 0x00, 0x3c, 0x66, 0x42, 0x7e, 0x40, 0x60, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x48, 0x48, 0x00, 0x00, 0x70, 0x10, 0x10, 0x10, 0x10, 0x18, 0x0e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x20, 0x30, 0x48, 0x00, 0x70, 0x10, 0x10, 0x10, 0x10, 0x18, 0x0e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x20, 0x30, 0x10, 0x00, 0x70, 0x10, 0x10, 0x10, 0x10, 0x18, 0x0e, 0x00, 0x00, 0x00,
    0x24, 0x24, 0x00, 0x18, 0x18, 0x3c, 0x24, 0x24, 0x66, 0x7e, 0x42, 0x42, 0xc3, 0x00, 0x00, 0x00,
    0x18, 0x24, 0x24, 0x18, 0x18, 0x34, 0x24, 0x24, 0x66, 0x7e, 0x42, 0x42, 0xc3, 0x00, 0x00, 0x00,
    0x18, 0x10, 0x00, 0x7e, 0x60, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0x60, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0x1a, 0x1b, 0x7f, 0x58, 0x48, 0x76, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1f, 0x18, 0x18, 0x28, 0x2e, 0x28, 0x78, 0x48, 0x48, 0x4f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x18, 0x24, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x24, 0x24, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x18, 0x08, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x18, 0x24, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x62, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x18, 0x08, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x62, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x24, 0x24, 0x00, 0x00, 0x42, 0x62, 0x26, 0x24, 0x34, 0x1c, 0x18, 0x08, 0x10, 0x70,
    0x24, 0x24, 0x00, 0x3c, 0x66, 0x42, 0x42, 0xc3, 0xc3, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x24, 0x24, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0x08, 0x1e, 0x60, 0x40, 0x40, 0x40, 0x40, 0x60, 0x1e, 0x08, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x1e, 0x30, 0x20, 0x20, 0x20, 0x7c, 0x20, 0x20, 0x20, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xc3, 0x42, 0x24, 0x24, 0x18, 0x7e, 0x18, 0x7e, 0x18, 0x18, 0x00, 0x00, 0x00,
    0x75, 0x5d, 0xc6, 0x98, 0x0e, 0x1f, 0x8f, 0xa7, 0x10, 0x85, 0xd7, 0x28, 0x00, 0xe6, 0x37, 0x2f,
    0x00, 0x00, 0x0f, 0x10, 0x10, 0x10, 0x7e, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x30, 0xe0,
    0x00, 0x00, 0x08, 0x18, 0x10, 0x00, 0x3c, 0x06, 0x02, 0x3e, 0x42, 0x42, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x18, 0x10, 0x00, 0x70, 0x10, 0x10, 0x10, 0x10, 0x18, 0x0e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x18, 0x10, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x18, 0x10, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x62, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x34, 0x2c, 0x00, 0x00, 0x7c, 0x46, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00,
    0x34, 0x2c, 0x00, 0x42, 0x62, 0x72, 0x52, 0x5a, 0x4a, 0x4a, 0x46, 0x46, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x04, 0x3c, 0x24, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x24, 0x42, 0x24, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x08, 0x08, 0x10, 0x30, 0x20, 0x60, 0x20, 0x3e,
    0xf7, 0xd1, 0xb5, 0x96, 0xae, 0xc6, 0x3b, 0x37, 0x5f, 0xd2, 0x29, 0x9c, 0x1f, 0x6f, 0x00, 0x84,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x62, 0x66, 0x24, 0x28, 0x08, 0x16, 0x31, 0x22, 0x44, 0x47, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x62, 0xa2, 0x24, 0x28, 0x08, 0x16, 0x16, 0x2a, 0x4f, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x10, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x24, 0x48, 0x24, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x24, 0x12, 0x24, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x42, 0xa5, 0x42, 0x00, 0x00, 0x42, 0xa5, 0x42, 0x00, 0x00, 0x42, 0xa5, 0x42, 0x00, 0x00,
    0x00, 0x38, 0xe7, 0x38, 0x00, 0x00, 0x38, 0xe7, 0x38, 0x00, 0x00, 0x38, 0xe7, 0x38, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xef, 0xff, 0xff, 0xff, 0xff, 0xef, 0xff, 0xff, 0xff, 0xff, 0xef, 0xf7,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x00, 0xf8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0xe4, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe4, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x00, 0xf8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0xe4, 0x04, 0xe4, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x04, 0xe4, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0xe4, 0x04, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0xe4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x00, 0x1f, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x27, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x27, 0x20, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x20, 0x27, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0xe7, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xe7, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x27, 0x20, 0x27, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0xe7, 0x00, 0xe7, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x1f, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x1f, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x27, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0xff, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 0x18, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x76, 0xb0, 0x08, 0x67, 0x80, 0x47, 0xff, 0x5d, 0x5e, 0xf3, 0x14, 0x9d, 0xcb, 0x5b, 0xf8, 0x2e,
    0x6e, 0xc9, 0x1e, 0x88, 0x3e, 0x9d, 0xc6, 0x2e, 0x7d, 0xef, 0x50, 0x5c, 0x19, 0x5f, 0xe3, 0xe5,
    0xfc, 0x1b, 0x26, 0xef, 0xc8, 0xe0, 0x43, 0x20, 0x89, 0x58, 0x62, 0x5e, 0x79, 0xba, 0xee, 0x7e,
    0x79, 0xf4, 0x4c, 0x72, 0xd2, 0x61, 0xba, 0x59, 0xa9, 0xdc, 0xaa, 0xce, 0xed, 0x52, 0x7a, 0x91,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x62, 0x42, 0x42, 0x42, 0x62, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3c, 0x64, 0x64, 0x4c, 0x58, 0x58, 0x4c, 0x42, 0x43, 0x43, 0x5e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3e, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x7c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7e, 0x60, 0x30, 0x10, 0x08, 0x18, 0x30, 0x20, 0x40, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x64, 0x42, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x62, 0x62, 0x7e, 0x40, 0x40, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x3c, 0x7e, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x7e, 0x3c, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x42, 0xdb, 0xc3, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x24, 0xe7, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x3e, 0x20, 0x30, 0x18, 0x24, 0x42, 0x42, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x5a, 0x5a, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x5a, 0x5a, 0xdb, 0x5a, 0x5a, 0x3c, 0x18, 0x18, 0x18,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x60, 0x60, 0x3c, 0x60, 0x60, 0x3e, 0x00, 0x00, 0x00,
    0x6c, 0xf2, 0x16, 0x6c, 0x4f, 0xb3, 0x5a, 0x88, 0x1d, 0xbc, 0xed, 0x23, 0xf1, 0x06, 0x66, 0x33,
    0x1a, 0x09, 0x68, 0x71, 0xae, 0x42, 0xcf, 0x19, 0xa5, 0x82, 0x7f, 0x27, 0xa2, 0x1b, 0x36, 0xe2,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x18, 0x00, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x30, 0x0c, 0x02, 0x0c, 0x30, 0x40, 0x00, 0x7e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x0c, 0x30, 0x40, 0x30, 0x0c, 0x02, 0x00, 0x7e, 0x00, 0x00, 0x00,
    0xfc, 0x04, 0xdc, 0x1e, 0x22, 0x91, 0x21, 0xdd, 0x8b, 0x71, 0x51, 0x6e, 0xf7, 0xce, 0xb8, 0xd8,
    0xee, 0x1d, 0x77, 0x64, 0x61, 0x40, 0x22, 0x9f, 0x9e, 0x7d, 0x84, 0xc8, 0x47, 0x6c, 0x1b, 0x3b,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x7e, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x72, 0x4e, 0x00, 0x72, 0x4e, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x24, 0x24, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x03, 0x02, 0x02, 0x02, 0x04, 0x64, 0x24, 0x24, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00,
    0xe6, 0x94, 0xd0, 0xc8, 0xb5, 0x57, 0xef, 0x34, 0xb9, 0xd5, 0x57, 0x53, 0x56, 0x87, 0x8d, 0x04,
    0x00, 0x00, 0x00, 0x1c, 0x24, 0x08, 0x10, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x7f, 0x40, 0x82, 0x3e, 0x9c, 0x4b, 0xea, 0x91, 0x4b, 0x7c, 0x24, 0xa4, 0x21, 0xd3, 0x83,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

font_data_t g_font_ubuntumono_16x8 = {
    .height = 16,
    .width = 8,
    .char_step = 1,
    .char_size = 16,
    .data   = glyph_data,
};
