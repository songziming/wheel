#include <rbtree.h>
#include <debug.h>


static inline void set_parent(rbnode_t *node, rbnode_t *parent) {
    node->parent_color = (node->parent_color & 1UL) | ((size_t)parent & ~1UL);
}

static inline void set_color(rbnode_t *node, size_t color) {
    node->parent_color = (color & 1UL) | (node->parent_color & ~1UL);
}


// 使用 child 替换 old 的位置，但不设置 parent 和颜色。child 可能为空，也可能在红黑树中
static void rb_change_child(rbtree_t *tree, rbnode_t *old, rbnode_t *child) {
    ASSERT(NULL != tree);
    ASSERT(NULL != old);

    rbnode_t *parent = RB_PARENT(old);
    if (NULL == parent) {
        ASSERT(tree->root == old);
        tree->root = child;
        return;
    }

    if (parent->left == old) {
        parent->left = child;
    } else {
        parent->right = child;
    }
}


// 参考 Linux Kernel __rb_erase_augmented
// 返回的节点再执行 __rb_erase_color
rbnode_t *rb_remove_augment(rbtree_t *tree, rbnode_t *node) {
    ASSERT(NULL != tree);
    ASSERT(NULL != node);

    rbnode_t *rebalance = NULL;

    if (NULL == node->left) {
        rbnode_t *right = node->right;
        rb_change_child(tree, node, right);

        if (NULL != right) {
            // 唯一的子节点必为红色，自己必为黑色
            // 用子节点替代自己，继承自己的颜色，维持黑高度不变
            ASSERT(RB_RED == RB_COLOR(right));
            ASSERT(RB_BLACK == RB_COLOR(node));
            right->parent_color = node->parent_color;
        } else if (RB_BLACK == RB_COLOR(node)) {
            // 删除一个黑节点，黑高度变化
            rebalance = RB_PARENT(node);
        }
    } else if (NULL == node->right) {
        // 已知左节点存在，右节点不存在，左节点必为红，自己必为黑
        rbnode_t *left = node->left;
        ASSERT(RB_RED == RB_COLOR(left));
        ASSERT(RB_BLACK == RB_COLOR(node));

        rb_change_child(tree, node, left);
        left->parent_color = node->parent_color;
    } else {
        // 左右子节点都存在，这种情况最复杂
        // 找出自己的后继，后继一定没有左节点
        rbnode_t *succ = node->right;
        rbnode_t *parent;
        rbnode_t *child2;
        if (NULL == succ->left) {
            // 右节点就是自己的后继
            //  node           succ     .
            //  /  \           /  \     .
            // L   succ  -->  L  child2 .
            //       \                  .
            //      child2              .
            parent = succ;
            child2 = succ->right;
        } else {
            // 右子树的最左节点就是 node 的后继
            //   node         succ  .
            //   /  \         /  \  .
            //  L    R  -->  L    R .
            //      /            /  .
            //  parent       parent .
            //    /            /    .
            // succ         child2  .
            //    \                 .
            //   child2             .
            while (NULL != succ->left) {
                parent = succ;
                succ = succ->left;
            }
            child2 = succ->right;
            parent->left = child2;
            succ->right = node->right;
            set_parent(node->right, succ);
        }

        succ->left = node->left;
        set_parent(node->left, succ);
        rb_change_child(tree, node, succ);

        if (NULL != child2) {
            set_parent(child2, parent);
            set_color(child2, RB_BLACK);
        } else if (RB_BLACK == RB_COLOR(succ)) {
            rebalance = parent;
        }
        succ->parent_color = node->parent_color;
    }

    return rebalance;
}
