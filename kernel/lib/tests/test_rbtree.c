#include <test.h>
#include <stdlib.h>

#include "../sources/rbtree.c"

typedef struct treeitem {
    rbnode_t rb;
    int      key;
} treeitem_t;

static rbtree_t mytree;

static void setup() {
    mytree = RBTREE_INIT;
}

static void free_subtree(treeitem_t *sub) {
    if (NULL != sub->rb.left) {
        free_subtree(containerof(sub->rb.left, treeitem_t, rb));
    }
    if (NULL != sub->rb.right) {
        free_subtree(containerof(sub->rb.right, treeitem_t, rb));
    }
    free(sub);
}

static void teardown() {
    if (NULL != mytree.root) {
        free_subtree(containerof(mytree.root, treeitem_t, rb));
    }
    mytree.root = NULL;
}

static treeitem_t *new_item(int key) {
    treeitem_t *item = malloc(sizeof(treeitem_t));
    item->rb = RBNODE_INIT;
    item->key = key;
    return item;
}

// 成功则返回 0，如果 key 已经存在，则返回 1，并且删除节点
static int insert_item(treeitem_t *item) {
    rbnode_t **link = &mytree.root;
    rbnode_t *parent = NULL;

    while (NULL != *link) {
        treeitem_t *curr = containerof(*link, treeitem_t, rb);

        parent = *link;
        if (item->key == curr->key) {
            free(item);
            return 1;
        } else if (item->key < curr->key) {
            link = &parent->left;
        } else {
            link = &parent->right;
        }
    }

    rb_insert(&mytree, &item->rb, parent, link);
    return 0;
}


//------------------------------------------------------------------------------
// 测试用例
//------------------------------------------------------------------------------

TEST_F(Tree, InsertNew, setup, teardown) {
    EXPECT_TRUE(0 == insert_item(new_item(1)));
    EXPECT_TRUE(0 == insert_item(new_item(2)));
    EXPECT_TRUE(0 == insert_item(new_item(3)));
    EXPECT_TRUE(0 == insert_item(new_item(4)));
}

TEST_F(Tree, InsertExisting, setup, teardown) {
    EXPECT_TRUE(0 == insert_item(new_item(1)));
    EXPECT_TRUE(0 == insert_item(new_item(2)));
    EXPECT_TRUE(1 == insert_item(new_item(1)));
    EXPECT_TRUE(0 == insert_item(new_item(3)));
    EXPECT_TRUE(1 == insert_item(new_item(2)));
}
