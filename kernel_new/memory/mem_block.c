#include "mem_block.h"
#include <library/debug.h>
#include "early_alloc.h"


// 记录物理内存布局
// 类似 linux/mm/memblock.c


typedef struct mem_block {
    size_t start;
    size_t end;
    mem_type_t type;
} mem_block_t;

static INIT_DATA int          g_block_max = 0;
static CONST     int          g_block_num = 0;
static CONST     mem_block_t *g_blocks    = NULL;


// 为 mem_block 预留空间
INIT_TEXT void mem_block_reserve(int n) {
    if (NULL != g_blocks) {
        log("warning: reallocating mem ranges array\n");
    }

    g_block_max = n;
    g_block_num = 0;
    g_blocks = early_alloc_ro(n * sizeof(mem_block_t));
}

// 记录一段物理内存，必须按从前到后的顺序调用
INIT_TEXT void mem_block_add(size_t start, size_t end, mem_type_t type) {
    if (g_block_num >= g_block_max) {
        log("warning: mem range overflow\n");
        return;
    }

    if (g_block_num > 0) {
        if (start < g_blocks[g_block_num - 1].end) {
            log("warning: %s out of order\n", __func__);
        }
    }

    mem_block_t *rng = &g_blocks[g_block_num++];

    rng->start = start;
    rng->end   = end;
    rng->type  = type;
}

// 获取所在 mem_block 的结束位置
// TODO 直接返回 mem_block 结构体更合适
INIT_TEXT size_t mem_block_end(size_t addr) {
    for (int i = 0; i < g_block_num; ++i) {
        if (g_blocks[i].start > addr) {
            continue;
        }
        if (addr < g_blocks[i].end) {
            return g_blocks[i].end;
        }
    }
    return 0;
}

// 获取所有可用内存的上限
INIT_TEXT size_t mem_block_top() {
    for (int i = g_block_num - 1; i >= 0; --i) {
        mem_type_t type = g_blocks[i].type;
        if ((MEM_AVAILABLE != type) && (MEM_RECLAIMABLE != type)) {
            continue;
        }
        return g_blocks[i].end;
    }
    return 0;
}

INIT_TEXT mem_type_t mem_block_type(size_t addr) {
    for (int i = 0; i < g_block_num; ++i) {
        if (g_blocks[i].start > addr) {
            continue;
        }
        if (addr < g_blocks[i].end) {
            return g_blocks[i].type;
        }
    }
    return MEM_RESERVED;
}

void mem_block_show() {
    for (int i = 0; i < g_block_num; ++i) {
        mem_block_t *rng = &g_blocks[i];
        log("start=0x%016lx, end=0x%016lx, type=%d\n",
            rng->start, rng->end, rng->type);
    }
}
