#ifndef POOL_SLUB_H
#define POOL_SLUB_H

#include <wheel.h>
#include <spinlock.h>
#include <page.h>

typedef struct pool {
    spin_t   lock;
    uint16_t obj_size;
    uint8_t  slab_order;        // slab 的页分配 rank
    pglist_t empty;             // 全部空闲
    pglist_t partial;           // 部分占用，按 ent_num 升序（空闲多的靠前）
    pglist_t full;              // 全部占用
} pool_t;

void  pool_init(pool_t *slub, size_t obj_size);
void  pool_destroy(pool_t *slub);
void  pool_shrink(pool_t *slub);
void *pool_alloc(pool_t *slub);
void  pool_free(pool_t *slub, void *obj);

#endif // POOL_SLUB_H
