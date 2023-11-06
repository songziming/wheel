#include <wheel.h>

static usize    kmem_align  = 0;
static int      kmem_count1 = 0;
static int      kmem_count2 = 0;
static pool_t * kmem_cache  = NULL;

#ifdef DEBUG
static int lib_initialized = NO;
#endif

static pool_t * get_pool(usize obj_size) {
    if (obj_size > PAGE_SIZE) {
        return NULL;
    }

    if (obj_size > kmem_align) {
        obj_size = ROUND_UP(obj_size, kmem_align);
        pool_t * pool = &kmem_cache[obj_size / kmem_align];
        if (-1 == pool->obj_size) {
            pool_init(pool, obj_size);
        }
        return pool;
    }

    // use `<=` instead of `<`, so range 33~64 is covered
    for (int i = 0; i <= kmem_count1; ++i) {
        if (obj_size <= (sizeof(usize) << i)) {
            obj_size = sizeof(usize) << i;
            pool_t * pool = &kmem_cache[i];
            if (-1 == pool->obj_size) {
                pool_init(pool, obj_size);
            }
            return pool;
        }
    }

    // make gcc happy
    return NULL;
}

void * kmem_alloc(usize obj_size) {
#ifdef DEBUG
    if (YES != lib_initialized) {
        panic("kmem lib not initialized.\n");
    }
#endif

    pool_t * pool = get_pool(obj_size);
    if (NULL == pool) {
        panic("obj_size %d not valid.\n", obj_size);
    }

    void * obj = pool_alloc(pool);
    if (NULL == obj) {
        for (int i = 0; i < (kmem_count1 + kmem_count2); ++i) {
            pool_shrink(&kmem_cache[i]);
        }
        obj = pool_alloc(pool);
    }

    return obj;
}

void kmem_free(usize obj_size, void * obj) {
#ifdef DEBUG
    if (YES != lib_initialized) {
        panic("kmem lib not initialized.\n");
    }
#endif

    pool_t * pool = get_pool(obj_size);
    if (NULL == pool) {
        panic("obj_size not valid.\n");
    }
    pool_free(pool, obj);
}

__INIT void kmem_lib_init(usize l1_size) {
#ifdef DEBUG
    if (YES == lib_initialized) {
        panic("kmem lib already initialized.\n");
    }
    lib_initialized = YES;
#endif

    assert(0 == (l1_size & (l1_size-1)));
    kmem_align = l1_size;

    kmem_count1 = CTZL(kmem_align) - CTZL(sizeof(usize));
    kmem_count2 = PAGE_SIZE / kmem_align;
    kmem_cache  = (pool_t *) allot_permanent((kmem_count1 + kmem_count2) * sizeof(pool_t));

    for (int i = 0; i < (kmem_count1 + kmem_count2); ++i) {
        kmem_cache[i].obj_size = -1;
    }
}
