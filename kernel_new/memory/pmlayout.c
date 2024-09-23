#include "pmlayout.h"
#include <library/debug.h>
#include "early_alloc.h"


// 记录物理内存布局
// 类似 linux/mm/memblock.c


static INIT_BSS int        g_range_max;
static CONST    int        g_range_num;
static CONST    pmrange_t *g_ranges    = NULL;


int pmrange_count() {
    return g_range_num;
}

pmrange_t *pmrange_at_index(int i) {
    ASSERT(i >= 0);
    ASSERT(i < g_range_num);
    return &g_ranges[i];
}

pmrange_t *pmrange_at_addr(size_t pa) {
    for (int i = 0; i < g_range_num; ++i) {
        if (g_ranges[i].start > pa) {
            continue;
        }
        if (pa < g_ranges[i].end) {
            return &g_ranges[i];
        }
    }
    return NULL;
}

// 为 mem_block 预留空间
INIT_TEXT void pmranges_alloc(int n) {
    ASSERT(NULL == g_ranges);

    g_range_max = n;
    g_range_num = 0;
    g_ranges = early_alloc_ro(n * sizeof(pmrange_t));
}

// 记录一段物理内存，必须按从前到后的顺序调用
INIT_TEXT void pmrange_add(size_t start, size_t end, pmtype_t type) {
    if (g_range_num >= g_range_max) {
        log("warning: mem range overflow\n");
        return;
    }

    if (g_range_num > 0) {
        if (start < g_ranges[g_range_num - 1].end) {
            log("warning: %s out of order\n", __func__);
        }
    }

    pmrange_t *rng = &g_ranges[g_range_num++];

    rng->start = start;
    rng->end   = end;
    rng->type  = type;
}

void pmlayout_show() {
    log("physical memory layout:\n");
    for (int i = 0; i < g_range_num; ++i) {
        pmrange_t *rng = &g_ranges[i];
        char *type;
        switch (rng->type) {
        case PM_AVAILABLE:   type = "available";   break;
        case PM_RECLAIMABLE: type = "reclaimable"; break;
        default:             type = "reserved";    break;
        }
        log("  - pm 0x%016lx~0x%016lx, %s\n", rng->start, rng->end, type);
    }
}
