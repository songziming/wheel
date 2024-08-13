#include "test.h"
#include <library/dllist.h>
#include <stdlib.h>
#include <string.h>



typedef struct listitem {
    dlnode_t dl;
    int      val;
} listitem_t;

// 头节点本身也是一个节点，位于链表中
static dlnode_t myhead;

static void setup() {
    dl_init_circular(&myhead);
}

static void teardown() {
    dlnode_t *node = myhead.next;
    while (node != &myhead) {
        listitem_t *item = containerof(node, listitem_t, dl);
        node = node->next;
        free(item);
    }
}

static listitem_t *new_item(int val) {
    listitem_t *item = malloc(sizeof(listitem_t));
    item->dl = DLNODE_INIT;
    item->val = val;
    return item;
}

static listitem_t *push_head(listitem_t *item) {
    EXPECT(!dl_contains(&myhead, &item->dl));
    dl_insert_after(&item->dl, &myhead);
    return item;
}

static listitem_t *push_tail(listitem_t *item) {
    EXPECT(!dl_contains(&myhead, &item->dl));
    dl_insert_before(&item->dl, &myhead);
    return item;
}

static listitem_t *pop_head() {
    dlnode_t *node = myhead.next;
    if (&myhead == node) {
        return NULL;
    }
    dl_remove(node);
    return containerof(node, listitem_t, dl);
}

static listitem_t *pop_tail() {
    dlnode_t *node = myhead.prev;
    if (&myhead == node) {
        return NULL;
    }
    dl_remove(node);
    return containerof(node, listitem_t, dl);
}

listitem_t *insert_before(listitem_t *item, listitem_t *ref) {
    EXPECT(!dl_contains(&myhead, &item->dl));
    if (NULL == ref) {
        dl_insert_before(&item->dl, &myhead);
    } else {
        EXPECT(dl_contains(&myhead, &ref->dl));
        dl_insert_before(&item->dl, &ref->dl);
    }
    return item;
}

listitem_t *insert_after(listitem_t *item, listitem_t *ref) {
    EXPECT(!dl_contains(&myhead, &item->dl));
    if (NULL == ref) {
        dl_insert_after(&item->dl, &myhead);
    } else {
        EXPECT(dl_contains(&myhead, &ref->dl));
        dl_insert_after(&item->dl, &ref->dl);
    }
    return item;
}

void remove_and_free(listitem_t *item) {
    EXPECT(dl_contains(&myhead, &item->dl));
    dl_remove(&item->dl);
    free(item);
}

void compare_array(const int *arr, int len) {
    dlnode_t *node = myhead.next;
    const int *it = arr;
    while (node != &myhead) {
        listitem_t *item = containerof(node, listitem_t, dl);
        node = node->next;
        EXPECT(item->val == *it++);
    }
    EXPECT(it == arr + len);
}

#define EXPECT_ARRAY(...)   do {    \
    int cmp[] = { __VA_ARGS__ };    \
    int len = sizeof(cmp) / sizeof(cmp[0]); \
    compare_array(cmp, len); \
} while (0)

void compare_item_and_free(listitem_t *item, int num) {
    EXPECT(item->val == num);
    free(item);
}


//------------------------------------------------------------------------------
// 测试用例
//------------------------------------------------------------------------------

TEST_F(List, PushHead, setup, teardown) {
    EXPECT(dl_is_lastone(&myhead));
    push_head(new_item(1));
    push_head(new_item(2));
    push_head(new_item(3));
    push_head(new_item(4));
    EXPECT_ARRAY(4,3,2,1);
}

TEST_F(List, PushTail, setup, teardown) {
    EXPECT(dl_is_lastone(&myhead));
    push_tail(new_item(1));
    push_tail(new_item(2));
    push_tail(new_item(3));
    push_tail(new_item(4));
    EXPECT_ARRAY(1,2,3,4);
}

TEST_F(List, PopHead, setup, teardown) {
    EXPECT(dl_is_lastone(&myhead));
    push_tail(new_item(1));
    push_tail(new_item(2));
    compare_item_and_free(pop_head(), 1);
    compare_item_and_free(pop_head(), 2);
    EXPECT(NULL == pop_head());
    EXPECT(dl_is_lastone(&myhead));
}

TEST_F(List, PopTail, setup, teardown) {
    EXPECT(dl_is_lastone(&myhead));
    push_tail(new_item(1));
    push_tail(new_item(2));
    compare_item_and_free(pop_tail(), 2);
    compare_item_and_free(pop_tail(), 1);
    EXPECT(NULL == pop_tail());
    EXPECT(dl_is_lastone(&myhead));
}

TEST_F(List, InsertBefore, setup, teardown) {
    EXPECT(dl_is_lastone(&myhead));
    listitem_t *i1 = insert_before(new_item(1), NULL);
    listitem_t *i2 = insert_before(new_item(2), i1);
    listitem_t *i3 = insert_before(new_item(3), i2);
    listitem_t *i4 = insert_before(new_item(4), i3);
    insert_before(new_item(5), i1);
    insert_before(new_item(6), i1);
    insert_before(new_item(7), i1);
    insert_before(new_item(8), i1);
    EXPECT_ARRAY(4,3,2,5,6,7,8,1);
}

TEST_F(List, InsertAfter, setup, teardown) {
    EXPECT(dl_is_lastone(&myhead));
    listitem_t *i1 = insert_after(new_item(1), NULL);
    listitem_t *i2 = insert_after(new_item(2), i1);
    listitem_t *i3 = insert_after(new_item(3), i2);
    listitem_t *i4 = insert_after(new_item(4), i3);
    insert_after(new_item(5), i1);
    insert_after(new_item(6), i1);
    insert_after(new_item(7), i1);
    insert_after(new_item(8), i1);
    EXPECT_ARRAY(1,8,7,6,5,2,3,4);
}

TEST_F(List, Pop, setup, teardown) {
    EXPECT(dl_is_lastone(&myhead));
    listitem_t *i1 = push_tail(new_item(1));
    listitem_t *i2 = push_tail(new_item(2));
    listitem_t *i3 = push_tail(new_item(3));
    listitem_t *i4 = push_tail(new_item(4));
    listitem_t *i5 = push_tail(new_item(5));
    listitem_t *i6 = push_tail(new_item(6));
    listitem_t *i7 = push_tail(new_item(7));
    listitem_t *i8 = push_tail(new_item(8));
    EXPECT_ARRAY(1,2,3,4,5,6,7,8);

    remove_and_free(i1);
    EXPECT_ARRAY(2,3,4,5,6,7,8);
    remove_and_free(i8);
    EXPECT_ARRAY(2,3,4,5,6,7);
    remove_and_free(i3);
    EXPECT_ARRAY(2,4,5,6,7);
    remove_and_free(i4);
    EXPECT_ARRAY(2,5,6,7);
}
