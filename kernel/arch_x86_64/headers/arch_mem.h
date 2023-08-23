#ifndef ARCH_MEM_H
#define ARCH_MEM_H

#include <base.h>


INIT_TEXT void early_alloc_init();

INIT_TEXT void ram_range_reserve(int num);
INIT_TEXT void ram_range_set(int idx, size_t addr, size_t len);
// INIT_TEXT int ram_range_has_overlap(size_t addr, size_t len);
INIT_TEXT size_t ram_range_get_limit(size_t addr);

#endif // ARCH_MEM_H
