#ifndef MEM_BLOCK_H
#define MEM_BLOCK_H

#include <common.h>

typedef enum mem_type {
    MEM_AVAILABLE,      // 可用
    MEM_RECLAIMABLE,    // 启动阶段不可用，初始化完成可用
    MEM_RESERVED,       // 不可用
} mem_type_t;

INIT_TEXT void mem_block_reserve(int n);
INIT_TEXT void mem_block_add(size_t start, size_t end, mem_type_t type);
INIT_TEXT size_t mem_block_end(size_t addr);
INIT_TEXT size_t mem_block_top();
INIT_TEXT mem_type_t mem_block_type(size_t addr);

void mem_block_show();

#endif // MEM_BLOCK_H
