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

// 从树中取出一个元素
static void remove_item(treeitem_t *item) {
    rb_remove(&mytree, &item->rb);
}


// 计算子树的黑高度，并验证黑高度是否一致
static int black_height(rbnode_t *rb) {
    if (NULL == rb) {
        return 1; // 叶子节点，是黑色的
    }

    // 黑高度必须相等
    int h1 = black_height(rb->left);
    int h2 = black_height(rb->right);
    EXPECT_TRUE(h1 == h2);

    // 红节点的两个子节点都是黑色
    if (RB_RED == RB_COLOR(rb)) {
        EXPECT_TRUE((NULL == rb->left) || (RB_BLACK == RB_COLOR(rb->left)));
        EXPECT_TRUE((NULL == rb->right) || (RB_BLACK == RB_COLOR(rb->right)));
        return h1;
    }

    return h1 + 1;
}

// 检查是否符合红黑树的五条性质
static void validate() {
    if (NULL == mytree.root) {
        return;
    }

    // 根节点必须是黑色
    EXPECT_TRUE(RB_BLACK == RB_COLOR(mytree.root));

    black_height(mytree.root);
}


//------------------------------------------------------------------------------
// 测试用例
//------------------------------------------------------------------------------

TEST_F(Tree, InsertNew, setup, teardown) {
    EXPECT_TRUE(0 == insert_item(new_item(1)));
    EXPECT_TRUE(0 == insert_item(new_item(2)));
    EXPECT_TRUE(0 == insert_item(new_item(3)));
    EXPECT_TRUE(0 == insert_item(new_item(4)));
    validate();
}

TEST_F(Tree, InsertExisting, setup, teardown) {
    EXPECT_TRUE(0 == insert_item(new_item(1)));
    EXPECT_TRUE(0 == insert_item(new_item(2)));
    EXPECT_TRUE(1 == insert_item(new_item(1)));
    EXPECT_TRUE(0 == insert_item(new_item(3)));
    EXPECT_TRUE(1 == insert_item(new_item(2)));
    validate();
}

TEST_F(Tree, RemoveLast, setup, teardown) {
    treeitem_t *item = new_item(100);
    insert_item(item);
    remove_item(item);
    free(item);
}
