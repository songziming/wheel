#ifndef SERIAL_H
#define SERIAL_H

#include <common.h>

INIT_TEXT void serial_init();
void serial_putc(char c);
void serial_puts(const char *s, size_t n);

#endif // SERIAL_H
