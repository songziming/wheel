#include <gtest/gtest.h>

extern "C" {
    #include "dllist.h"
}



struct ListItem {
    dlnode_t dl;
    int      val;

    explicit ListItem(int v) : val(v) {}
};

class DLListTest : public ::testing::Test {
public:
    dlnode_t head_; // 头节点本身也是一个节点，位于链表中
protected:
    void SetUp() override;
    void TearDown() override;

    bool contains(ListItem *item) {
        return 0 != dl_contains(&head_, &item->dl);
    }

    ListItem *push_head(ListItem *item);
    ListItem *push_tail(ListItem *item);

    ListItem *pop_head();
    ListItem *pop_tail();

    ListItem *insert_before(ListItem *item, ListItem *ref);
    ListItem *insert_after(ListItem *item, ListItem *ref);

    void remove_and_free(ListItem *item);
    void compare_array(const int *arr, int len);
};


void DLListTest::SetUp() {
    dl_init_circular(&head_);
}

void DLListTest::TearDown() {
    dlnode_t *node = head_.next;
    while (node != &head_) {
        ListItem *item = containerof(node, ListItem, dl);
        node = node->next;
        delete item;
    }
}

ListItem *DLListTest::push_head(ListItem *item) {
    EXPECT_FALSE(dl_contains(&head_, &item->dl));
    dl_insert_after(&item->dl, &head_);
    return item;
}

ListItem *DLListTest::push_tail(ListItem *item) {
    EXPECT_FALSE(dl_contains(&head_, &item->dl));
    dl_insert_before(&item->dl, &head_);
    return item;
}

ListItem *DLListTest::pop_head() {
    dlnode_t *node = head_.next;
    if (&head_ == node) {
        return NULL;
    }
    dl_remove(node);
    return containerof(node, ListItem, dl);
}

ListItem *DLListTest::pop_tail() {
    dlnode_t *node = head_.prev;
    if (&head_ == node) {
        return NULL;
    }
    dl_remove(node);
    return containerof(node, ListItem, dl);
}

ListItem *DLListTest::insert_before(ListItem *item, ListItem *ref) {
    EXPECT_FALSE(dl_contains(&head_, &item->dl));
    if (NULL == ref) {
        dl_insert_before(&item->dl, &head_);
    } else {
        EXPECT_TRUE(dl_contains(&head_, &ref->dl));
        dl_insert_before(&item->dl, &ref->dl);
    }
    return item;
}

ListItem *DLListTest::insert_after(ListItem *item, ListItem *ref) {
    EXPECT_FALSE(dl_contains(&head_, &item->dl));
    if (NULL == ref) {
        dl_insert_after(&item->dl, &head_);
    } else {
        EXPECT_TRUE(dl_contains(&head_, &ref->dl));
        dl_insert_after(&item->dl, &ref->dl);
    }
    return item;
}

void DLListTest::remove_and_free(ListItem *item) {
    EXPECT_TRUE(dl_contains(&head_, &item->dl));
    dl_remove(&item->dl);
    delete item;
}

void DLListTest::compare_array(const int *arr, int len) {
    dlnode_t *node = head_.next;
    const int *it = arr;
    while (node != &head_) {
        ListItem *item = containerof(node, ListItem, dl);
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

void compare_item_and_free(ListItem *item, int num) {
    EXPECT_TRUE(item->val == num);
    delete item;
}


//------------------------------------------------------------------------------
// 测试用例
//------------------------------------------------------------------------------

TEST_F(DLListTest, Init) {
    EXPECT_EQ(head_.prev, &head_);
    EXPECT_EQ(head_.next, &head_);
}

TEST(DLList, RemoveLast) {
    dlnode_t myhead;
    dl_init_circular(&myhead);
    dl_remove(&myhead);
    EXPECT_EQ(NULL, myhead.prev);
    EXPECT_EQ(NULL, myhead.next);
}

TEST_F(DLListTest, Remove) {
    ListItem item1{1};
    ListItem item2{2};
    ListItem item3{3};

    push_tail(&item1);
    push_tail(&item2);
    push_tail(&item3);
    EXPECT_TRUE(contains(&item1));
    EXPECT_TRUE(contains(&item2));
    EXPECT_TRUE(contains(&item3));

    dlnode_t *next = dl_remove(&item2.dl);
    EXPECT_EQ(&item3.dl, next);

    dl_remove(&item1.dl);
    dl_remove(&item3.dl);
    EXPECT_TRUE(dl_is_lastone(&head_));

    // // 删除最后一个元素
    // dl_remove(&head_);
    // EXPECT_EQ(NULL, head_.prev);
    // EXPECT_EQ(NULL, head_.next);
}

TEST_F(DLListTest, PushHead) {
    EXPECT_TRUE(dl_is_lastone(&head_));
    push_head(new ListItem{1});
    push_head(new ListItem{2});
    push_head(new ListItem{3});
    push_head(new ListItem{4});
    EXPECT_ARRAY(4,3,2,1);
}

TEST_F(DLListTest, PushTail) {
    EXPECT_TRUE(dl_is_lastone(&head_));
    push_tail(new ListItem{1});
    push_tail(new ListItem{2});
    push_tail(new ListItem{3});
    push_tail(new ListItem{4});
    EXPECT_ARRAY(1,2,3,4);
}

TEST_F(DLListTest, PopHead) {
    EXPECT_TRUE(dl_is_lastone(&head_));
    push_tail(new ListItem{1});
    push_tail(new ListItem{2});
    compare_item_and_free(pop_head(), 1);
    compare_item_and_free(pop_head(), 2);
    EXPECT_TRUE(NULL == pop_head());
    EXPECT_TRUE(dl_is_lastone(&head_));
}

TEST_F(DLListTest, PopTail) {
    EXPECT_TRUE(dl_is_lastone(&head_));
    push_tail(new ListItem{1});
    push_tail(new ListItem{2});
    compare_item_and_free(pop_tail(), 2);
    compare_item_and_free(pop_tail(), 1);
    EXPECT_TRUE(NULL == pop_tail());
    EXPECT_TRUE(dl_is_lastone(&head_));
}

TEST_F(DLListTest, InsertBefore) {
    EXPECT_TRUE(dl_is_lastone(&head_));
    ListItem *i1 = insert_before(new ListItem{1}, NULL);
    ListItem *i2 = insert_before(new ListItem{2}, i1);
    ListItem *i3 = insert_before(new ListItem{3}, i2);
    ListItem *i4 = insert_before(new ListItem{4}, i3);
    insert_before(new ListItem{5}, i1);
    insert_before(new ListItem{6}, i1);
    insert_before(new ListItem{7}, i1);
    insert_before(new ListItem{8}, i1);
    EXPECT_ARRAY(4,3,2,5,6,7,8,1);
}

TEST_F(DLListTest, InsertAfter) {
    EXPECT_TRUE(dl_is_lastone(&head_));
    ListItem *i1 = insert_after(new ListItem{1}, NULL);
    ListItem *i2 = insert_after(new ListItem{2}, i1);
    ListItem *i3 = insert_after(new ListItem{3}, i2);
    ListItem *i4 = insert_after(new ListItem{4}, i3);
    insert_after(new ListItem{5}, i1);
    insert_after(new ListItem{6}, i1);
    insert_after(new ListItem{7}, i1);
    insert_after(new ListItem{8}, i1);
    EXPECT_ARRAY(1,8,7,6,5,2,3,4);
}

TEST_F(DLListTest, Pop) {
    EXPECT_TRUE(dl_is_lastone(&head_));
    ListItem *i1 = push_tail(new ListItem{1});
    ListItem *i2 = push_tail(new ListItem{2});
    ListItem *i3 = push_tail(new ListItem{3});
    ListItem *i4 = push_tail(new ListItem{4});
    ListItem *i5 = push_tail(new ListItem{5});
    ListItem *i6 = push_tail(new ListItem{6});
    ListItem *i7 = push_tail(new ListItem{7});
    ListItem *i8 = push_tail(new ListItem{8});
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
