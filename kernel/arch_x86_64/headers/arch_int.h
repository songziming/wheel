#ifndef ARCH_INT_H
#define ARCH_INT_H

#include <def.h>
#include <arch_extra.h>

typedef void (*int_handler_t)(int vec, arch_regs_t *f);

INIT_TEXT void int_init();

void set_int_handler(int vec, int_handler_t handler);

#endif // ARCH_INT_H
