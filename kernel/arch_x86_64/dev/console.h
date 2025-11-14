#ifndef ARCH_X86_64_DEV_CONSOLE_H
#define ARCH_X86_64_DEV_CONSOLE_H

#include <wheel.h>

INIT_TEXT void console_init();
void console_puts(const char *s, size_t n);

#endif // ARCH_X86_64_DEV_CONSOLE_H