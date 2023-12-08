#ifndef DEV_FRAMEBUF_H
#define DEV_FRAMEBUF_H

#include <base.h>
#include <multiboot2.h>

typedef struct font_data {
    int            width;
    int            height;
    int            char_step;   // 字符一行的字节数
    int            char_size;   // 字符总字节数
    const uint8_t *data;
} font_data_t;

INIT_TEXT void framebuf_init(mb2_tag_framebuffer_t *fb);
void framebuf_putc(char ch);
void framebuf_puts(const char *s, size_t n);

#endif // DEV_FRAMEBUF_H
