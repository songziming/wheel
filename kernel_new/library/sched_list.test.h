// 有序链表测试用例
// 可用于多种实现的测试

TEST(LIST_NAME, Diff) {
    item_t item1 = { .priority = 1 };
    item_t item2 = { .priority = 2 };
    item_t item3 = { .priority = 3 };
    item_t item4 = { .priority = 4 };
    item_t item5 = { .priority = 5 };

    LIST_INIT();
    EXPECT_EQ(NULL, LIST_HEAD());

    LIST_INSERT(item3); // 3
    EXPECT_TRUE(LIST_CONTAINS(item3));
    EXPECT_EQ(&item3, LIST_HEAD());

    LIST_INSERT(item5); // 3 5
    EXPECT_TRUE(LIST_CONTAINS(item5));
    EXPECT_EQ(&item3, LIST_HEAD());

    LIST_INSERT(item2); // 2 3 5
    EXPECT_TRUE(LIST_CONTAINS(item2));
    EXPECT_EQ(&item2, LIST_HEAD());

    LIST_INSERT(item1); // 1 2 3 5
    EXPECT_TRUE(LIST_CONTAINS(item1));
    EXPECT_EQ(&item1, LIST_HEAD());

    LIST_REMOVE(item1); // 2 3 5
    EXPECT_FALSE(LIST_CONTAINS(item1));
    EXPECT_EQ(&item2, LIST_HEAD());

    LIST_REMOVE(item3); // 2 5
    EXPECT_FALSE(LIST_CONTAINS(item3));
    EXPECT_EQ(&item2, LIST_HEAD());
}

TEST(LIST_NAME, Same) {
    item_t item1a = { .priority = 1 };
    item_t item1b = { .priority = 1 };
    item_t item1c = { .priority = 1 };
    item_t item2a = { .priority = 2 };
    item_t item2b = { .priority = 2 };
    item_t item2c = { .priority = 2 };

    LIST_INIT();
    EXPECT_EQ(NULL, LIST_HEAD());

    LIST_INSERT(item2a); // 2a
    EXPECT_TRUE(LIST_CONTAINS(item2a));
    EXPECT_EQ(&item2a, LIST_HEAD());

    LIST_INSERT(item2b); // 2a 2b
    EXPECT_TRUE(LIST_CONTAINS(item2b));
    EXPECT_EQ(&item2a, LIST_HEAD());

    LIST_INSERT(item2c); // 2a 2b 2c
    EXPECT_TRUE(LIST_CONTAINS(item2c));
    EXPECT_EQ(&item2a, LIST_HEAD());

    LIST_INSERT(item1a); // 1a 2a 2b 2c
    EXPECT_TRUE(LIST_CONTAINS(item1a));
    EXPECT_EQ(&item1a, LIST_HEAD());

    LIST_INSERT(item1b); // 1a 1b 2a 2b 2c
    EXPECT_TRUE(LIST_CONTAINS(item1b));
    EXPECT_EQ(&item1a, LIST_HEAD());

    LIST_INSERT(item1c); // 1a 1b 1c 2a 2b 2c
    EXPECT_TRUE(LIST_CONTAINS(item1c));
    EXPECT_EQ(&item1a, LIST_HEAD());

    LIST_REMOVE(item1c); // 1a 1b 2a 2b 2c
    EXPECT_FALSE(LIST_CONTAINS(item1c));
    EXPECT_EQ(&item1a, LIST_HEAD());

    LIST_REMOVE(item1a); // 1b 2a 2b 2c
    EXPECT_FALSE(LIST_CONTAINS(item1a));
    EXPECT_EQ(&item1b, LIST_HEAD());

    LIST_REMOVE(item1b); // 2a 2b 2c
    EXPECT_FALSE(LIST_CONTAINS(item1b));
    EXPECT_EQ(&item2a, LIST_HEAD());
}
