#ifndef ARCH_X86_64_DEV_FRAMEBUF_H
#define ARCH_X86_64_DEV_FRAMEBUF_H

#include <wheel.h>

typedef struct fbfont {
    int            rows;    // 字符高度（单位：像素）
    int            cols;    // 字符宽度（单位：像素）
    int            pitch;   // 字符一行的字节数
    int            size;    // 字符总字节数
    const uint8_t *data;
} fbfont_t;

INIT_TEXT void framebuf_init(uint32_t rows, uint32_t cols, uint32_t pitch, uint32_t addr);
INIT_TEXT void framebuf_remap_wc();
void framebuf_setfg(uint32_t fg);
// void framebuf_puts(const char *s, size_t n);

#endif // ARCH_X86_64_DEV_FRAMEBUF_H
