#ifndef ARCH_INT_H
#define ARCH_INT_H

#include <base.h>
#include <arch_interface.h>

typedef void (*int_handler_t)(int_context_t *ctx);

extern int_handler_t g_handlers[256];

INIT_TEXT void int_init();

#endif // ARCH_INT_H
