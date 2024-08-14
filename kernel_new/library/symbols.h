#ifndef SYMBOLS_H
#define SYMBOLS_H

#include <common.h>

INIT_TEXT void parse_kernel_symtab(void *ptr, uint32_t entsize, unsigned num);
size_t sym_locate(const char *name);
const char *sym_resolve(size_t addr, size_t *rela);
// void dump_symbols();

#endif // SYMBOLS_H
