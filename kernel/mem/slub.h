#ifndef SLUB_H
#define SLUB_H

#include <wheel.h>
#include <spin.h>
#include <page.h>

//-----------------------------------------------------------------------------
// SLUB 对象缓存
//
// 不设独立的 slab 管理结构，slab 元数据直接存入 page_t 的
// prev / next / ent_num / objects 字段（零额外 slab 头开销）。
// 空闲 freelist 以 2-byte offset 链的形式嵌在对象体内。
//
// slab 按占用情况分属三个链表：
//  - empty   : 全部未分配，ent_num==0
//  - partial : 部分分配，按 ent_num 升序排列
//  - full    : 全部已分配，ent_num==MAX
// 也可以认为这是一个链表，只是分成了三个子序列，提供跳表可以快速索引
//
// 分配时优先从 partial 头部（空闲最多）取，释放时按 ent_num 重排
// 链表以维持这个顺序。empty 和 full 链表不分顺序。
//-----------------------------------------------------------------------------

typedef struct slub {
    spin_t   lock;
    uint16_t obj_size;
    uint8_t  slab_order;        // slab 的页分配 rank
    pglist_t empty;             // 全部空闲
    pglist_t partial;           // 部分占用，按 ent_num 升序（空闲多的靠前）
    pglist_t full;              // 全部占用
} slub_t;

void  slub_init(slub_t *slub, size_t obj_size);
void  slub_destroy(slub_t *slub);
void  slub_shrink(slub_t *slub);
void *slub_alloc(slub_t *slub);
void  slub_free(slub_t *slub, void *obj);

#endif // SLUB_H
