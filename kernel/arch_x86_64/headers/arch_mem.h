#ifndef ARCH_MEM_H
#define ARCH_MEM_H

#include <def.h>


// 临时内存分配

INIT_TEXT void early_rw_unlock();
INIT_TEXT void early_alloc_disable();



// 物理内存布局

typedef enum pmtype {
    PM_RESERVED = 0,
    PM_AVAILABLE,
    PM_RECLAIMABLE,
} pmtype_t;

typedef struct pmrange {
    pmtype_t type;
    size_t addr;
    size_t end;
} pmrange_t;

extern CONST int g_pmmap_len;
extern CONST pmrange_t *g_pmmap;

INIT_TEXT void pmmap_init_mb1(uint32_t mmap, uint32_t len);
INIT_TEXT void pmmap_init_mb2(void *tag);

INIT_TEXT pmrange_t *pmmap_locate(size_t ptr);

#ifdef DEBUG
INIT_TEXT void pmmap_show();
#endif



// Per-CPU

INIT_TEXT size_t pcpu_init(size_t end);
INIT_TEXT void gsbase_init(int idx);




INIT_TEXT void mem_init();
INIT_TEXT void ctx_init();

void reclaim_init();

#endif // ARCH_MEM_H
