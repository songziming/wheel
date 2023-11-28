#ifndef ARCH_MEM_H
#define ARCH_MEM_H

#include <def.h>

typedef enum ram_type {
    RAM_RESERVED = 0,
    RAM_AVAILABLE,
    RAM_RECLAIMABLE,
} ram_type_t;

typedef struct ram_range {
    ram_type_t type;
    size_t addr;
    size_t end;
} ram_range_t;

extern CONST int g_rammap_len;
extern CONST ram_range_t *g_rammap;

INIT_TEXT int rammap_hasoverlap(size_t addr, size_t len);
#ifdef DEBUG
INIT_TEXT void rammap_show();
#endif

INIT_TEXT void early_alloc_init();
INIT_TEXT void early_rw_unlock();
INIT_TEXT void mem_init();

#endif // ARCH_MEM_H
