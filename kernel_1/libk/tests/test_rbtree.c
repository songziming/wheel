#include <test.h>
#include <stdlib.h>
#include <libk.h>

#include "../sources/rbtree.c"


typedef struct rbitem {
    rbnode_t rb;
    int      val;
} rbitem_t;

static rbitem_t *new_item(int val) {
    rbitem_t *item = malloc(sizeof(rbitem_t));
    memset(&item->rb, 0, sizeof(rbnode_t));
    item->val = val;
    return item;
}

static rbtree_t mytree;

static void setup() {
    memset(&mytree, 0, sizeof(rbtree_t));
}

static void deleteSubTree(rbnode_t *node) {
    if (NULL == node) {
        return;
    }
    deleteSubTree(node->left);
    deleteSubTree(node->right);
    rbitem_t *item = containerof(node, rbitem_t, rb);
    free(item);
}

static void teardown() {
    deleteSubTree(mytree.root);
}



rbitem_t *insert(rbitem_t *item) {
    rbnode_t **link = &mytree.root;
    rbnode_t  *node = NULL;

    while (NULL != *link) {
        node = *link;
        rbitem_t *cmp = containerof(node, rbitem_t, rb);
        if (item->val == cmp->val) {
            EXPECT_TRUE(0, "same value not allowed\n");
            return item;
        } else if (item->val < cmp->val) {
            link = &node->left;
        } else {
            link = &node->right;
        }
    }

    rb_insert(&mytree, &item->rb, node, link);
    return item;
}

static const int *walk_subtree(rbnode_t *node, const int *it) {
    if (NULL == node) {
        return it;
    }

    rbitem_t *item = containerof(node, rbitem_t, rb);

    it = walk_subtree(node->left, it);
    EXPECT_TRUE(item->val == *it++);
    it = walk_subtree(node->right, it);

    return it;
}

void compare_list(const int *cmp, int len) {
    const int *it = walk_subtree(mytree.root, cmp);
    EXPECT_TRUE(it == cmp + len);
}

#define EXPECT_LIST(...) do { \
    int cmp[] = { __VA_ARGS__ };    \
    int len = sizeof(cmp) / sizeof(cmp[0]); \
    compare_list(cmp, len); \
} while (0)


TEST_F(Tree, Basic, setup, teardown) {
    insert(new_item(1));
    insert(new_item(2));
    insert(new_item(3));
    insert(new_item(4));
    insert(new_item(5));
    insert(new_item(6));
    insert(new_item(7));
    EXPECT_LIST(1,2,3,4,5,6,7);
}
