// 基于红黑树的动态内存分配器
// 内核堆，用于分配不固定大小的内存（如字符串）

#include <heap.h>
#include <wheel.h>


#define ALIGNMENT 8
#define ROUND_UP(x) (((x) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))
#define ROUND_DOWN(x) ((x) & ~(ALIGNMENT - 1))

typedef struct chunk_hdr {
    uint32_t prevsize;   // 包括 header
    uint32_t selfsize;   // 包括 header，最低位表示已分配
} chunk_hdr_t;

typedef struct chunk {
    chunk_hdr_t hdr;
    union {
        uint8_t data[8];    // 已分配 chunk 拥有此成员
        struct {
            dlnode_t freenode;
            rbnode_t sizenode;
        };
    };
} ALIGNED(ALIGNMENT) chunk_t;

#define CHUNK_INUSE 1U // 表示已分配



static inline chunk_t *build_chunk_used(size_t addr, size_t prevsize, size_t selfsize) {
    ASSERT(prevsize <= UINT32_MAX);
    ASSERT(selfsize <= UINT32_MAX);
    ASSERT(selfsize >= sizeof(chunk_hdr_t));
    ASSERT(0 == (prevsize & (ALIGNMENT - 1)));
    ASSERT(0 == (selfsize & (ALIGNMENT - 1)));

    chunk_t *chk = (chunk_t *)addr;
    chk->hdr.prevsize = prevsize;
    chk->hdr.selfsize = selfsize | CHUNK_INUSE;
    return chk;
}

static inline chunk_t *build_chunk_free(size_t addr, size_t prevsize, size_t selfsize) {
    ASSERT(prevsize <= UINT32_MAX);
    ASSERT(selfsize <= UINT32_MAX);
    ASSERT(selfsize >= sizeof(chunk_t));
    ASSERT(0 == (prevsize & (ALIGNMENT - 1)));
    ASSERT(0 == (selfsize & (ALIGNMENT - 1)));

    chunk_t *chk = (chunk_t *)addr;
    chk->hdr.prevsize = prevsize;
    chk->hdr.selfsize = selfsize;
    dl_init_circular(&chk->freenode);
    chk->sizenode = RBNODE_INIT;
    return chk;
}




// 将 chunk 放入未分配集合
// 不会检查相邻 chunk
static void put_chunk_into_heap(mem_heap_t *heap, chunk_t *chk) {
    ASSERT(NULL != heap);
    ASSERT(NULL != chk);
    // ASSERT(chk->hdr.selfsize & CHUNK_INUSE);

    chk->hdr.selfsize &= ~CHUNK_INUSE; // 标记为可用
    // chk->sizenode = RBNODE_INIT;

    // 在 sizetree 中搜索相同大小的节点
    rbnode_t **link = &heap->sizetree.root;
    rbnode_t *parent = NULL;
    while (NULL != *link) {
        parent = *link;
        chunk_t *ref = containerof(parent, chunk_t, sizenode);
        size_t size = ref->hdr.selfsize;

        if (chk->hdr.selfsize == size) {
            // 已存在相同大小的 freelist，只需添加到链表结尾
            chk->sizenode.parent_color = 0;
            dl_insert_before(&chk->freenode, &ref->freenode);
            return;
        } else if (chk->hdr.selfsize < size) {
            link = &parent->left;
        } else {
            link = &parent->right;
        }
    }

    // 没有该大小的 bin，创建新的 freelist
    // 此 chunk 作为新的 size-node 放入平衡树
    chk->sizenode = RBNODE_INIT;
    dl_init_circular(&chk->freenode);
    rb_insert(&heap->sizetree, &chk->sizenode, parent, link);
}

// 从集合中取出一个 chunk，标记为已分配
static void take_chunk_from_heap(mem_heap_t *heap, chunk_t *chk) {
    ASSERT(NULL != heap);
    ASSERT(NULL != chk);
    // ASSERT(0 == (chk->hdr.selfsize & CHUNK_INUSE));

    chk->hdr.selfsize |= CHUNK_INUSE; // 标记为已分配

    // 检查 parent 指针，判断是否位于 sizetree（红黑树根节点为黑色，非零）
    if (chk->sizenode.parent_color) {
        dlnode_t *next_dl = dl_remove(&chk->freenode);
        if (NULL == next_dl) {
            rb_remove(&heap->sizetree, &chk->sizenode);
        } else {
            chunk_t *next_chk = containerof(next_dl, chunk_t, freenode);
            rb_replace(&heap->sizetree, &chk->sizenode, &next_chk->sizenode);
        }
        chk->sizenode = RBNODE_INIT; // 已从红黑树中取出
    } else {
        // 不是 freelist 头节点，只要将它从链表中删除
        dl_remove(&chk->freenode);
    }
}


static chunk_t *chunk_alloc(mem_heap_t *heap, size_t size) {
    ASSERT(NULL != heap);
    ASSERT(0 == (size & (ALIGNMENT - 1)));

    // size += sizeof(chunk_hdr_t);
    // size = ROUND_UP(size);

    // 在红黑树中寻找最合适的节点（最小的的大于 size 的 chunk）
    rbnode_t *rb = heap->sizetree.root;
    chunk_t *best = NULL;
    while (rb) {
        chunk_t *chk = containerof(rb, chunk_t, sizenode);
        ASSERT(rb->parent_color);
        ASSERT(0 == (chk->hdr.selfsize & CHUNK_INUSE));

        if (chk->hdr.selfsize < size) {
            rb = rb->right;
        } else if (chk->hdr.selfsize > size) {
            best = chk;
            rb = rb->left;
        } else {
            best = chk;
            break;
        }
    }

    if (NULL == best) {
        // 内存不足，需要扩容
        return NULL;
    }

    // 取出 freelist 中的最后一个节点，尽量不移除头节点
    chunk_t *chk = containerof(best->freenode.prev, chunk_t, freenode);
    take_chunk_from_heap(heap, chk);

    // 如果剩余空间足够大，就分割为前后两个 chunk，后一部分放回 heap
    size_t remain = chk->hdr.selfsize - size;
    if (remain >= sizeof(chunk_t))  {
        chunk_t *rest = build_chunk_free((size_t)chk + size, size, remain);
        put_chunk_into_heap(heap, rest);

        chunk_t *next = (chunk_t *)((size_t)chk + chk->hdr.selfsize);
        ASSERT(chk->hdr.selfsize == next->hdr.prevsize);
        next->hdr.prevsize = remain;

        chk->hdr.selfsize = size;
    }

    return chk;
}


static void chunk_free(mem_heap_t *heap, chunk_t *chk) {
    ASSERT(NULL != heap);
    ASSERT(NULL != chk);
    ASSERT(0 == ((size_t)chk & (ALIGNMENT - 1)));

    // size_t addr = (size_t)ptr - sizeof(chunk_hdr_t);
    // ASSERT(0 == (addr & (ALIGNMENT - 1)));

    // chunk_t *chk = (chunk_t *)addr;
    ASSERT(chk->hdr.selfsize & CHUNK_INUSE);
    size_t size = chk->hdr.selfsize & ~CHUNK_INUSE;
    ASSERT(0 == (size & (ALIGNMENT - 1)));

    chunk_t *prev = (chunk_t *)((size_t)chk - chk->hdr.selfsize);
    chunk_t *next = (chunk_t *)((size_t)chk + size);

    if (0 == (prev->hdr.selfsize & CHUNK_INUSE)) {
        take_chunk_from_heap(heap, prev);
        next->hdr.prevsize += size;
        prev->hdr.selfsize += size;
        chk = prev;
    }

    if (0 == (next->hdr.selfsize & CHUNK_INUSE)) {
        take_chunk_from_heap(heap, next);
        chunk_t *after = (chunk_t *)((size_t)next + next->hdr.selfsize);
        after->hdr.prevsize += chk->hdr.selfsize;
        chk->hdr.selfsize += next->hdr.selfsize;
    }

    put_chunk_into_heap(heap, chk);
}



// 在给定内存片段建立堆
// TODO 如果传入 buff 是空指针，表示自动分配
//      自动分配的 heap 支持自动扩容
void heap_init(mem_heap_t *heap, void *buff, size_t size) {
    ASSERT(NULL != heap);
    ASSERT(NULL != buff);

    if (size > UINT32_MAX) {
        klog("warning: heap buffer truncated!\n");
        size = UINT32_MAX;
    }

    size_t start = ROUND_UP((size_t)buff);
    size_t end = ROUND_DOWN((size_t)buff + size);
    ASSERT(start + sizeof(chunk_t) * 3 <= end);

    size_t guard_size = ROUND_UP(sizeof(chunk_hdr_t));

    // 划分为三个 chunk，头尾始终处于 inuse 状态
    build_chunk_used(start, 0, guard_size);
    start += guard_size;
    end -= guard_size;
    chunk_t *body_chk = build_chunk_free(start, guard_size, end - start);
    build_chunk_used(end, end - start, guard_size);

    heap->spin = SPIN_INIT;
    heap->sizetree = RBTREE_INIT;
    put_chunk_into_heap(heap, body_chk);
}

void *heap_alloc(mem_heap_t *heap, size_t size) {
    ASSERT(NULL != heap);

    size += sizeof(chunk_hdr_t);
    size = ROUND_UP(size);

    int key = irq_spin_take(&heap->spin);
    chunk_t *chk = chunk_alloc(heap, size);
    irq_spin_give(&heap->spin, key);

    if (NULL == chk) {
        return NULL;
    }
    return chk->data;
}

void heap_free(mem_heap_t *heap, void *ptr) {
    ASSERT(NULL != heap);
    ASSERT(NULL != ptr);

    chunk_t *chk = (chunk_t *)((size_t)ptr - sizeof(chunk_hdr_t));

    int key = irq_spin_take(&heap->spin);
    chunk_free(heap, chk);
    irq_spin_give(&heap->spin, key);
}
