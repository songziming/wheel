#ifndef ARCH_INT_H
#define ARCH_INT_H

#include <common.h>
#include "arch_impl.h"

INIT_TEXT void int_init();

void set_int_handler(int vec, void (*handler)(int, regs_t *));

#endif // ARCH_INT_H
