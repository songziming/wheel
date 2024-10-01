#ifndef FRAMEBUF_H
#define FRAMEBUF_H

#include <common.h>

typedef struct font_data {
    int            rows;    // 字符高度（单位：像素）
    int            cols;    // 字符宽度（单位：像素）
    int            pitch;   // 字符一行的字节数
    int            size;    // 字符总字节数
    const uint8_t *data;
} font_data_t;

INIT_TEXT void framebuf_init(uint32_t rows, uint32_t cols, uint32_t pitch, uint32_t addr);
void framebuf_set_color(uint32_t fg);
void framebuf_puts(const char *s, size_t n);

#endif // FRAMEBUF_H
