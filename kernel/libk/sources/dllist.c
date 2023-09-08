// 双向循环无头链表
// 也可以将一个节点当作头节点来使用

#include <dllist.h>
#include <debug.h>


// 由一个节点构成自环
dlnode_t *dl_init_circular(dlnode_t *node) {
    ASSERT(NULL != node);
    node->prev = node;
    node->next = node;
    return node;
}

// 判断是否为单个节点构成的环
int dl_is_lastone(dlnode_t *node) {
    ASSERT(NULL != node);
    ASSERT(NULL != node->prev);
    ASSERT(NULL != node->next);
    return (node->prev == node) && (node->next == node);
}

dlnode_t *dl_insert_before(dlnode_t *node, dlnode_t *ref) {
    ASSERT(NULL != node);
    ASSERT(NULL != ref);
    ASSERT(node != ref);

    dlnode_t *prev = ref->prev;
    dlnode_t *next = ref;
    ASSERT(NULL != prev);
    ASSERT(node != prev);

    prev->next = node;
    next->prev = node;
    node->prev = prev;
    node->next = next;

    return node;
}

dlnode_t *dl_insert_after(dlnode_t *node, dlnode_t *ref) {
    ASSERT(NULL != node);
    ASSERT(NULL != ref);
    ASSERT(node != ref);

    dlnode_t *prev = ref;
    dlnode_t *next = ref->next;
    ASSERT(NULL != next);
    ASSERT(node != next);

    prev->next = node;
    next->prev = node;
    node->prev = prev;
    node->next = next;

    return node;
}

// 将 node 从双链表中取出
dlnode_t *dl_remove(dlnode_t *node) {
    ASSERT(NULL != node);

    dlnode_t *prev = node->prev;
    dlnode_t *next = node->next;
    ASSERT(NULL != prev);
    ASSERT(NULL != next);
    ASSERT(((prev == node) && (next == node))
        || ((prev != node) && (next != node)));

    if ((prev != node) && (next != node)) {
        prev->next = next;
        next->prev = prev;
    }

    // 全零表示不在任何双链表中
    node->prev = NULL;
    node->next = NULL;

    return node;
}
