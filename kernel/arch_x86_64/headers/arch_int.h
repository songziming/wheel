#ifndef ARCH_INT_H
#define ARCH_INT_H

#include <def.h>

extern PCPU_BSS uint8_t  g_int_stack[];
extern PCPU_BSS int      g_int_depth;
extern PCPU_BSS uint64_t g_int_rsp;

INIT_TEXT void int_init();

#endif // ARCH_INT_H
