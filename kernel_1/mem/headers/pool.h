#ifndef MEM_POOL_H
#define MEM_POOL_H

#include <base.h>
#include <page.h>
#include <dllist.h>

typedef struct pool {
    size_t      obj_size;   // 已对齐到 2 的幂
    uint8_t     block_rank; // 分配的基本单位
    page_list_t blocks;
    dlnode_t    free;   // 未分配的 object 组成的链表
} pool_t;

#endif // MEM_POOL_H
