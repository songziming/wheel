#include <pool.h>
#include <vmspace.h>
#include <debug.h>
#include <libk.h>


// TODO 不一定按照一个 PAGE 的大小申请物理页，可以使用更大的 rank，根据 obj_size 动态决定
// TODO 改为每个块都有一个 free-list，申请内存时，优先使用剩余 object 最少的块
// TODO 回收内存时，找到 object 所属的块，将 object 放回所在块的 freelist
//      如果某个块中所有 object 都已经回收，则可以将这个块回收


// 复杂版本的内存池初始化函数，可以设置每次内存分配的大小
void pool_init_ex(pool_t *p, size_t size, uint8_t rank) {
    if (size < sizeof(dlnode_t)) {
        size = sizeof(dlnode_t);
    }

    size +=  15;
    size &= ~15;

    p->obj_size = size;
    p->block_rank = rank;
    p->blocks = PAGE_LIST_EMPTY;
    dl_init_circular(&p->free);
}


void pool_init(pool_t *p, size_t size) {
    if (size > PAGE_SIZE) {
        dbg_print("object size %zu too large!\n", size);
        return;
    }

    pool_init_ex(p, size, 0);
}

// 从内存池里分配一个对象
void *pool_allocate_object(pool_t *p) {
    if (!dl_is_lastone(&p->free)) {
        dlnode_t *last = p->free.prev;
        return (void *)dl_remove(last);
    }

    // 没有空闲的object，需要申请新的物理页
    pfn_t pg = page_cache_alloc(PT_POOL);

    // 没有使用 vmspace 申请带有保护页的虚拟地址范围
    // 因为对等映射才能方便地转换物理地址
    size_t va = DIRECT_MAP_BASE + ((size_t)pg << PAGE_SHIFT);
    memset((void *)va, 0, PAGE_SIZE);

    // 逐一将object添加到链表
    for (size_t i = 0; i < PAGE_SIZE; i += p->obj_size) {
        dlnode_t *node = (dlnode_t *)(va + i);
        dl_insert_before(node, &p->free);
    }

    // 重新取出链表中最后一个元素
    return (void *)dl_remove(p->free.prev);
}

void pool_free_object(pool_t *p, void *obj) {
    dlnode_t *node = (dlnode_t *)obj;
    dl_insert_before(node, &p->free);
}
