#ifndef ARCH_X86_64_DEV_SERIAL_H
#define ARCH_X86_64_DEV_SERIAL_H

#include <wheel.h>

INIT_TEXT void serial_init();
void serial_puts(const char *s, size_t n);

#endif // ARCH_X86_64_DEV_SERIAL_H