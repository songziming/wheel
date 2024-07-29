#ifndef CONSOLE_H
#define CONSOLE_H

#include <common.h>

INIT_TEXT void console_init();
void console_putc(char c);
void console_puts(const char *s, size_t n);

#endif // CONSOLE_H
