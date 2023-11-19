#ifndef DEV_SERIAL_H
#define DEV_SERIAL_H

#include <def.h>

INIT_TEXT void serial_init();
void serial_putc(char c);
void serial_puts(const char *s, size_t n);

#endif // DEV_SERIAL_H
