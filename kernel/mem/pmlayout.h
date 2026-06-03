#ifndef PMLAYOUT_H
#define PMLAYOUT_H

#include <wheel.h>

typedef enum pmtype {
    PM_AVAILABLE,   // 可用
    PM_RECLAIMABLE, // 启动阶段不可用，初始化完成可用
    PM_RESERVED,    // 不可用
} pmtype_t;

typedef struct pmrange {
    size_t   start;
    size_t   end;
    pmtype_t type;
} pmrange_t;

extern int        g_pmrange_num;
extern pmrange_t *g_pmranges;

pmrange_t *pmrange_at(size_t pa);
void pmlayout_show();

#endif // PMLAYOUT_H
