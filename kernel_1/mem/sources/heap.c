// 堆内存分配器，支持分配不同大小的 object
// 更适合用户层，或者会随着任务整体删除的情况，如果长时间存在，碎片会不断累积
// 内核分配内存应该使用 pool

#include <base.h>
#include <debug.h>

#include <dllist.h>
#include <rbtree.h>


#define ALIGNMENT 16
#define ROUND_UP(x) (((x) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))
#define ROUND_DOWN(x) ((x) & ~(ALIGNMENT - 1))


typedef struct mem_heap {
    rbtree_t size_tree;
} mem_heap_t;

typedef struct chunk_hdr {
    size_t prev_size;   // 包括 header
    size_t self_size;   // 包括 header，最低两比特有特殊含义
} chunk_hdr_t;

typedef struct chunk {
    chunk_hdr_t hdr;
    union {
        uint8_t data[ALIGNMENT];    // 已分配 chunk 拥有此成员
        struct {
            dlnode_t free_node;     // 未分配 chunk 拥有此成员
            rbnode_t size_node;     // 未分配 HEAD chunk 拥有此成员
        };
    };
} chunk_t;


enum chunk_flag {
    FREE = 1,   // 未分配
    HEAD = 3,   // 特殊的未分配 chunk，freelist 中的第一个节点，位于 size_tree 内部
};

#define CHK_SIZE(chk)   ((chk)->hdr.self_size & ~3UL)
#define CHK_FREE(chk)   ((chk)->hdr.self_size & FREE)
#define CHK_HEAD(chk)   ((chk)->hdr.self_size & HEAD)





static inline chunk_t *chunk_init_used(size_t addr, size_t prev_size, size_t self_size) {
    chunk_t *chk = (chunk_t *)addr;
    chk->hdr.prev_size = prev_size;
    chk->hdr.self_size = self_size;
    return chk;
}

static inline chunk_t *chunk_init_free(size_t addr, size_t prev_size, size_t self_size) {
    chunk_t *chk = chunk_init_used(addr, prev_size, self_size);
    chk->hdr.self_size |= FREE;
    dl_init_circular(&chk->free_node);
    chk->size_node = RBNODE_INIT;
    return chk;
}

// static inline chunk_t *chunk_init_head(size_t addr, size_t prev_size, size_t self_size) {
//     chunk_t *chk = chunk_init_free(addr, prev_size, self_size);
//     chk->hdr.self_size |= HEAD;
//     return chk;
// }




// 将 chunk 放入未分配集合
// 不会检查相邻 chunk
static void put_chunk_into_heap(mem_heap_t *heap, chunk_t *chk) {
    ASSERT(NULL != heap);
    ASSERT(NULL != chk);

    size_t size = CHK_SIZE(chk);
    chk->hdr.self_size = size | FREE;

    // 在 size_tree 中搜索相同大小的节点
    rbnode_t **link = &heap->size_tree.root;
    rbnode_t  *node = NULL;
    while (NULL != *link) {
        node = *link;
        chunk_t *bin_chk = containerof(node, chunk_t, size_node);
        size_t bin_size = CHK_SIZE(bin_chk);

        if (size == bin_size) {
            // 已存在相同大小的 freelist，只需添加到链表结尾
            dl_insert_before(&chk->free_node, &bin_chk->free_node);
            // chk->free_node = DLNODE_INIT;
            // dl_push_tail(&bin_chk->free_list, &chk->free_node);
            return;
        } else if (size < bin_size) {
            link = &node->left;
        } else {
            link = &node->right;
        }
    }

    // 没有该大小的 bin，创建新的 freelist
    chk->hdr.self_size |= HEAD;
    chk->size_node = RBNODE_INIT;
    // chk->free_list = DLLIST_INIT;
    rb_insert(&heap->size_tree, &chk->size_node, node, link);
}

// 从集合中取出一个 chunk
static void get_chunk_from_heap(mem_heap_t *heap, chunk_t *chk) {
    ASSERT(NULL != heap);
    ASSERT(NULL != chk);
    ASSERT(chk->hdr.self_size & FREE);

    if (chk->hdr.self_size & HEAD) {
        dlnode_t *next_dl = dl_remove(&chk->free_node);
        if (NULL == next_dl) {
            rb_remove(&heap->size_tree, &chk->size_node);
        } else {
            chunk_t *next_chk = containerof(next_dl, chunk_t, free_node);
            rb_replace(&heap->size_tree, &chk->size_node, &next_chk->size_node);
            next_chk->hdr.self_size |= HEAD;
        }
    } else {
        dl_remove(&chk->free_node);
    }

    chk->hdr.self_size = CHK_SIZE(chk);
}




// 在给定内存片段建立堆
void heap_init(mem_heap_t *heap, size_t start, size_t end) {
    start = ROUND_UP(start);
    end = ROUND_DOWN(end);

    size_t guard_size = ROUND_UP(sizeof(chunk_hdr_t));
    // size_t chunk_min = ROUND_UP(sizeof(chunk_t));

    // 划分为三个 chunk，头尾始终处于 used 状态
    chunk_t *head_chk = (chunk_t *)start;
    start += guard_size;
    chunk_t *body_chk = (chunk_t *)start;
    end -= guard_size;
    chunk_t *tail_chk = (chunk_t *)end;

    head_chk->hdr.prev_size = 0;
    head_chk->hdr.self_size = guard_size;           // 已分配
    body_chk->hdr.prev_size = guard_size;
    body_chk->hdr.self_size = end - start + FREE;   // 未分配
    tail_chk->hdr.prev_size = end - start;
    tail_chk->hdr.self_size = guard_size;           // 已分配

    heap->size_tree = RBTREE_INIT;
    put_chunk_into_heap(heap, body_chk);
}


void *heap_alloc(mem_heap_t *heap, size_t size) {
    size += sizeof(chunk_hdr_t) + ALIGNMENT - 1;
    size /= ALIGNMENT;

    // 在红黑树中寻找最合适的节点
    rbnode_t *node = heap->size_tree.root;
    chunk_t *best = NULL;
    while (NULL != node) {
        chunk_t *chk = containerof(node, chunk_t, size_node);
        ASSERT(chk->hdr.self_size & FREE);
        ASSERT(chk->hdr.self_size & HEAD);

        size_t chk_size = CHK_SIZE(chk);

        if (chk_size < size) {
            node = node->right;
        } else if (chk_size > size) {
            best = chk;
            node = node->left;
        } else {
            best = chk;
            break;
        }
    }

    if (NULL == best) {
        // 内存不足，需要扩容
        // 例如申请新的 zone
        return NULL;
    }

    // 从集合中取出
    // TODO 不应取出这个节点，而应取出 freelist 中的前一个节点
    //      尽量不替换 rbnode
    get_chunk_from_heap(heap, best);

    // 如果剩余空间足够大，就分割
    size_t chunk_min = (sizeof(chunk_t) + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    size_t rest_size = best->hdr.self_size - size;
    if (rest_size >= chunk_min)  {
        chunk_t *rest = chunk_init_free((size_t)best + size, size, rest_size);
        put_chunk_into_heap(heap, rest);

        chunk_t *next = (chunk_t *)((size_t)best + best->hdr.self_size);
        next->hdr.prev_size -= size;

        best->hdr.self_size = size;
    }

    return best->data;
}

void heap_free(mem_heap_t *heap, void *ptr) {
    ASSERT(NULL != heap);
    ASSERT(NULL != ptr);

    size_t addr = (size_t)ptr - sizeof(chunk_hdr_t);
    ASSERT(0 == (addr & (ALIGNMENT - 1)));

    chunk_t *self = (chunk_t *)addr;
    size_t size = self->hdr.self_size;
    ASSERT(0 == (size & 3));

    chunk_t *prev = (chunk_t *)(addr - size);
    chunk_t *next = (chunk_t *)(addr + size);

    if (FREE & prev->hdr.self_size) {
        get_chunk_from_heap(heap, prev);
        next->hdr.prev_size += size;
        prev->hdr.self_size += size;
        self = prev;
    }

    if (FREE & next->hdr.self_size) {
        get_chunk_from_heap(heap, next);
        chunk_t *after = (chunk_t *)((size_t)next + next->hdr.self_size);
        after->hdr.prev_size += self->hdr.self_size;
        self->hdr.self_size += next->hdr.self_size;
    }

    put_chunk_into_heap(heap, self);
}
