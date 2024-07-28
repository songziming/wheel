#ifndef MEM_MAP_H
#define MEM_MAP_H

#include <common.h>

typedef enum mem_type {
    MEM_AVAILABLE,      // 可用
    MEM_RECLAIMABLE,    // 启动阶段不可用，初始化完成可用
    MEM_RESERVED,       // 不可用
} mem_type_t;

INIT_TEXT void mem_map_reserve(int n);
INIT_TEXT void mem_map_add(size_t start, size_t end, mem_type_t type);

#endif // MEM_MAP_H
