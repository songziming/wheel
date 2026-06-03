#include "pmlayout.h"
#include <debug.h>


CONST int        g_pmrange_num = 0;
CONST pmrange_t *g_pmranges    = NULL;

pmrange_t *pmrange_at(size_t pa) {
    for (int i = 0; i < g_pmrange_num; ++i) {
        if (g_pmranges[i].start > pa) {
            continue;
        }
        if (pa < g_pmranges[i].end) {
            return &g_pmranges[i];
        }
    }
    return NULL;
}

void pmlayout_show() {
    logk("physical memory layout:\n");
    for (int i = 0; i < g_pmrange_num; ++i) {
        pmrange_t *rng = &g_pmranges[i];
        char *type;
        switch (rng->type) {
        case PM_AVAILABLE:   type = "available";   break;
        case PM_RECLAIMABLE: type = "reclaimable"; break;
        default:             type = "reserved";    break;
        }
        logk("  - pm 0x%016lx~0x%016lx, %s\n", rng->start, rng->end, type);
    }
}
