#ifndef ARCH_MEM_H
#define ARCH_MEM_H

#include <base.h>

INIT_TEXT void *early_alloc_ro(size_t size);
INIT_TEXT void *early_alloc_rw(size_t size);
INIT_TEXT void early_alloc_unlock();

typedef enum ram_type {
    RAM_RESERVED = 0,
    RAM_AVAILABLE,
    RAM_RECLAIMABLE,
} ram_type_t;

INIT_TEXT void ram_map_reserve(int num);
INIT_TEXT void ram_map_set(int idx, ram_type_t type, size_t addr, size_t len);
#if DEBUG
INIT_TEXT void ram_map_show();
#endif

#endif // ARCH_MEM_H
