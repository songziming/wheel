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

INIT_TEXT void framebuf_init(fb_info_t *fb);

#endif // DEV_FRAMEBUF_H
