#include <wheel.h>

// currently all levels of cache are power of 2
// this is very wasteful
// maybe divide levels of cache by object count in one page

// 10 levels of object size, from 8 bytes to 4K
#define CACHE_COUNT 10

static pool_t caches[CACHE_COUNT];

static int cache_index(usize obj_size) {
    obj_size -= 1;
    obj_size |= obj_size >>  1;
    obj_size |= obj_size >>  2;
    obj_size |= obj_size >>  4;
    obj_size |= obj_size >>  8;
    obj_size |= obj_size >> 16;
    obj_size |= obj_size >> 32;
    obj_size += 1;
    return CTZL(obj_size);
}

void * kmem_alloc(usize obj_size) {
    int idx = cache_index(obj_size);
    if (idx >= CACHE_COUNT) {
        return NULL;
    }

    void * obj = pool_alloc(&caches[idx]);
    if (NULL == obj) {
        for (int i = 0; i < CACHE_COUNT; ++i) {
            pool_shrink(&caches[i]);
        }
        obj = pool_alloc(&caches[idx]);
    }

    return obj;
}

void kmem_free(usize obj_size, void * obj) {
    int idx = cache_index(obj_size);
    assert(idx < 10);
    pool_free(&caches[idx], obj);
}

__INIT void kmem_lib_init() {
    for (int i = 0; i < CACHE_COUNT; ++i) {
        pool_init(&caches[i], sizeof(usize) << i);
    }
}
