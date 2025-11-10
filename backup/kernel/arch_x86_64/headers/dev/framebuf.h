#ifndef DEV_FRAMEBUF_H
#define DEV_FRAMEBUF_H

#include <def.h>

typedef struct fb_info {
    size_t   addr;
    unsigned rows;
    unsigned cols;
    unsigned bpp;   // 一个像素多少比特
    unsigned pitch; // 一行多少字节，可能不是整数个像素
    struct {
        uint8_t mask_size;
        uint8_t position;
    } r, g, b;
} fb_info_t;

typedef struct font_data {
    int            width;
    int            height;
    int            char_step;   // 字符一行的字节数
    int            char_size;   // 字符总字节数
    const uint8_t *data;
} font_data_t;

INIT_TEXT void framebuf_init(const fb_info_t *fb);
void framebuf_putc(char c);
void framebuf_puts(const char *s, size_t n);

#endif // DEV_FRAMEBUF_H
