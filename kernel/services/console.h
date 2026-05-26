#ifndef CONSOLE_H
#define CONSOLE_H

#include <wheel.h>

// 终端驱动接口，需要 arch 实现
typedef struct display_ops {
    int width;
    int height;
    void (*draw_char)(char ch, int x, int y);
    void (*draw_caret)(int x, int y);
    void (*scroll)(int nlines);
} display_ops_t;

extern display_ops_t *g_display;

void console_puts(const char *s, size_t n);
void console_printf(const char *fmt, ...) PRINTF(1,2);
void console_readline(char *dst, size_t max);
INIT_TEXT void console_init();

#endif // CONSOLE_H
