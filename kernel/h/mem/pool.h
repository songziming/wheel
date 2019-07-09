#ifndef MEM_POOL_H
#define MEM_POOL_H

#include <base.h>
#include <core/spin.h>
#include <mem/page.h>

typedef struct pool {
    spin_t   lock;
    int      obj_size;
    int      blk_order;
    pglist_t full;          // all objects not allocated
    pglist_t partial;       // some objects allocated
    pglist_t empty;         // all objects allocated
} pool_t;

extern void pool_init   (pool_t * pool, usize obj_size);
extern void pool_destroy(pool_t * pool);
extern void pool_shrink (pool_t * pool);

extern void * pool_alloc(pool_t * pool);
extern void   pool_free (pool_t * pool, void * obj);

#endif // MEM_POOL_H
