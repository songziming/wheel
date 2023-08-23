#ifndef DEV_OUT_SERIAL_H
#define DEV_OUT_SERIAL_H

#include <base.h>

INIT_TEXT void serial_init();
void serial_putc(char c);
void serial_puts(const char *s, size_t n);

#endif // DEV_OUT_SERIAL_H
