#ifndef ARCH_INT_H
#define ARCH_INT_H

#include <def.h>

extern PCPU_BSS uint8_t int_stack[];
extern PCPU_DATA int g_int_depth;

#endif // ARCH_INT_H
