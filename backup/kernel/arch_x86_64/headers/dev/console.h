#ifndef DEV_CONSOLE_H
#define DEV_CONSOLE_H

#include <def.h>

INIT_TEXT void console_init();
void console_putc(char c);
void console_puts(const char *s, size_t n);

#endif // DEV_CONSOLE_H
