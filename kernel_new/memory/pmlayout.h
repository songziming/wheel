#ifndef PMLAYOUT_H
#define PMLAYOUT_H

#include <common.h>

typedef enum pmtype {
    PM_AVAILABLE,   // 可用
    PM_RECLAIMABLE, // 启动阶段不可用，初始化完成可用
    PM_RESERVED,    // 不可用
} pmtype_t;

typedef struct pmrange {
    size_t start;
    size_t end;
    pmtype_t type;
} pmrange_t;

int pmrange_count();
pmrange_t *pmrange_at_index(int i);
pmrange_t *pmrange_at_addr(size_t pa);

INIT_TEXT void pmranges_alloc(int n);
INIT_TEXT void pmrange_add(size_t start, size_t end, pmtype_t type);

void pmlayout_show();

#endif // PMLAYOUT_H
