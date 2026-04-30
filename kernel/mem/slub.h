#ifndef SLUB_H
#define SLUB_H

#include <wheel.h>
#include <spin.h>
#include <page.h>


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
