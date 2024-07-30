#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <common.h>

void parse_kernel_symtab(void *ptr, uint32_t entsize, unsigned num);
void dump_symbols();

#endif // SYMBOLS_H
