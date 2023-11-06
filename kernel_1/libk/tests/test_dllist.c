#include <test.h>
#include <stdlib.h>
#include <string.h>

#include "../sources/dllist.c"


typedef struct myitem {
    dlnode_t dl;
    int      val;
} myitem_t;

// 头节点本身也是一个节点，位于链表中
static dlnode_t myhead;

static void setup() {
    dl_init_circular(&myhead);
}

static void teardown() {
    dlnode_t *node = myhead.next;
    while (node != &myhead) {
        myitem_t *item = containerof(node, myitem_t, dl);
        node = node->next;
        free(item);
    }
}

static myitem_t *new_item(int val) {
    myitem_t *item = malloc(sizeof(myitem_t));
    memset(&item->dl, 0, sizeof(dlnode_t));
    item->val = val;
    return item;
}

static myitem_t *push_head(myitem_t *item) {
    dl_insert_after(&item->dl, &myhead);
    return item;
}

static myitem_t *push_tail(myitem_t *item) {
    dl_insert_before(&item->dl, &myhead);
    return item;
}

static myitem_t *pop_head() {
    if (myhead.next == &myhead) {
        return NULL;
    }
    dlnode_t *node = dl_remove(myhead.next);
    return containerof(node, myitem_t, dl);
}

static myitem_t *pop_tail() {
    if (myhead.prev == &myhead) {
        return NULL;
    }
    dlnode_t *node = dl_remove(myhead.prev);
    return containerof(node, myitem_t, dl);
}

myitem_t *insert_before(myitem_t *item, myitem_t *ref) {
    if (NULL == ref) {
        dl_insert_before(&item->dl, &myhead);
    } else {
        dl_insert_before(&item->dl, &ref->dl);
    }
    return item;
}

myitem_t *insert_after(myitem_t *item, myitem_t *ref) {
    if (NULL == ref) {
        dl_insert_after(&item->dl, &myhead);
    } else {
        dl_insert_after(&item->dl, &ref->dl);
    }
    return item;
}

void remove_and_free(myitem_t *item) {
    dl_remove(&item->dl);
    free(item);
}

void compare_array(const int *arr, int len) {
    dlnode_t *node = myhead.next;
    const int *it = arr;
    while (node != &myhead) {
        myitem_t *item = containerof(node, myitem_t, dl);
        node = node->next;
        EXPECT_TRUE(item->val == *it++);
    }
    EXPECT_TRUE(it == arr + len);
}

#define EXPECT_ARRAY(...)   do {    \
    int cmp[] = { __VA_ARGS__ };    \
    int len = sizeof(cmp) / sizeof(cmp[0]); \
    compare_array(cmp, len); \
} while (0)

void compare_item_and_free(myitem_t *item, int num) {
    EXPECT_TRUE(item->val == num);
    free(item);
}


//------------------------------------------------------------------------------
// 测试用例
//------------------------------------------------------------------------------

TEST_F(List, PushHead, setup, teardown) {
    push_head(new_item(1));
    push_head(new_item(2));
    push_head(new_item(3));
    push_head(new_item(4));
    EXPECT_ARRAY(4,3,2,1);
}

TEST_F(List, PushTail, setup, teardown) {
    push_tail(new_item(1));
    push_tail(new_item(2));
    push_tail(new_item(3));
    push_tail(new_item(4));
    EXPECT_ARRAY(1,2,3,4);
}

TEST_F(List, PopHead, setup, teardown) {
    push_tail(new_item(1));
    push_tail(new_item(2));
    compare_item_and_free(pop_head(), 1);
    compare_item_and_free(pop_head(), 2);
    EXPECT_TRUE(NULL == pop_head());
}

TEST_F(List, PopTail, setup, teardown) {
    push_tail(new_item(1));
    push_tail(new_item(2));
    compare_item_and_free(pop_tail(), 2);
    compare_item_and_free(pop_tail(), 1);
    EXPECT_TRUE(NULL == pop_tail());
}

TEST_F(List, InsertBefore, setup, teardown) {
    myitem_t *i1 = insert_before(new_item(1), NULL);
    myitem_t *i2 = insert_before(new_item(2), i1);
    myitem_t *i3 = insert_before(new_item(3), i2);
    myitem_t *i4 = insert_before(new_item(4), i3);
    insert_before(new_item(5), i1);
    insert_before(new_item(6), i1);
    insert_before(new_item(7), i1);
    insert_before(new_item(8), i1);
    EXPECT_ARRAY(4,3,2,5,6,7,8,1);
}

TEST_F(List, InsertAfter, setup, teardown) {
    myitem_t *i1 = insert_after(new_item(1), NULL);
    myitem_t *i2 = insert_after(new_item(2), i1);
    myitem_t *i3 = insert_after(new_item(3), i2);
    myitem_t *i4 = insert_after(new_item(4), i3);
    insert_after(new_item(5), i1);
    insert_after(new_item(6), i1);
    insert_after(new_item(7), i1);
    insert_after(new_item(8), i1);
    EXPECT_ARRAY(1,8,7,6,5,2,3,4);
}

TEST_F(List, Pop, setup, teardown) {
    myitem_t *i1 = push_tail(new_item(1));
    myitem_t *i2 = push_tail(new_item(2));
    myitem_t *i3 = push_tail(new_item(3));
    myitem_t *i4 = push_tail(new_item(4));
    myitem_t *i5 = push_tail(new_item(5));
    myitem_t *i6 = push_tail(new_item(6));
    myitem_t *i7 = push_tail(new_item(7));
    myitem_t *i8 = push_tail(new_item(8));
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
