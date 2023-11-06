#ifndef DEV_OUT_CONSOLE_H
#define DEV_OUT_CONSOLE_H

#include <base.h>

INIT_TEXT void console_init();
void console_putc(char c);
void console_puts(const char *s, size_t n);

#endif // DEV_OUT_CONSOLE_H
