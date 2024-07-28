#include "mem_map.h"
#include <debug.h>
#include "early_alloc.h"


// 记录物理内存布局
// 类似 linux/mm/memblock.c


typedef struct mem_range {
    size_t start;
    size_t end;
    mem_type_t type;
} mem_range_t;

static INIT_DATA int          g_mem_range_max = 0;
static CONST     int          g_mem_range_num = 0;
static CONST     mem_range_t *g_mem_ranges    = NULL;


// 为 mem_range 预留空间
INIT_TEXT void mem_map_reserve(int n) {
    if (NULL != g_mem_ranges) {
        log("warning: reallocating mem ranges array\n");
    }

    g_mem_range_max = n;
    g_mem_range_num = 0;
    g_mem_ranges = early_alloc_ro(n * sizeof(mem_range_t));
}

// 记录一段物理内存，必须按从前到后的顺序调用
INIT_TEXT void mem_map_add(size_t start, size_t end, mem_type_t type) {
    if (g_mem_range_num >= g_mem_range_max) {
        log("warning: mem range overflow\n");
        return;
    }

    if (g_mem_range_num > 0) {
        if (start < g_mem_ranges[g_mem_range_num - 1].end) {
            log("warning: %s out of order\n", __func__);
        }
    }

    mem_range_t *rng = &g_mem_ranges[g_mem_range_num++];

    rng->start = start;
    rng->end   = end;
    rng->type  = type;
}
