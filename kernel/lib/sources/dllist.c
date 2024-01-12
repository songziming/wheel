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

// 判断节点是否位于链表中（自环也是合法的链表）
int dl_is_wired(dlnode_t *node) {
    ASSERT(NULL != node);
    return (NULL != node->prev) && (NULL != node->next);
}

// 判断节点是否位于链表中
int dl_contains(dlnode_t *head, dlnode_t *node) {
    ASSERT(NULL != head);
    ASSERT(NULL != node);
    ASSERT(head != node);
    for (dlnode_t *i = head->next; head != i; i = i->next) {
        if (node == i) {
            return 1;
        }
    }
    return 0;
}

void dl_insert_before(dlnode_t *node, dlnode_t *ref) {
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
}

void dl_insert_after(dlnode_t *node, dlnode_t *ref) {
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
}

// 将 node 从双链表中取出，返回链表中的下一个元素
// 如果 node 就是最后一个元素，则返回空指针
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
        return next;
    }

    return NULL;
}
