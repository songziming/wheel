// 红黑树，参考了 Linux Kernel 和算法导论

#include <rbtree.h>
#include <debug.h>


static inline void set_parent(rbnode_t *node, rbnode_t *parent) {
    node->parent_color = (node->parent_color & 1UL) | ((size_t)parent & ~1UL);
}

static inline void set_color(rbnode_t *node, size_t color) {
    node->parent_color = (color & 1UL) | (node->parent_color & ~1UL);
}


// 二叉树左旋
//     P             P      .
//     |             |      .
//     X             Y      .
//    / \    -->    / \     .
//   a   Y         X   c    .
//      / \       / \       .
//     b   c     a   b      .
static void rb_rotate_left(rbtree_t *tree, rbnode_t *node) {
    rbnode_t *X = node;
    rbnode_t *Y = X->right;
    rbnode_t *P = RB_PARENT(X);

    if (NULL != (X->right = Y->left)) { // X->right  = b
        set_parent(Y->left, X);         // b->parent = X
    }
    Y->left = X;                        // Y->left   = X
    set_parent(Y, P);                   // Y->parent = P
    set_parent(X, Y);                   // X->parent = Y

    if (NULL == P) {
        tree->root = Y;
    } else if (X == P->left) {
        P->left = Y;
    } else {
        P->right = Y;
    }
}


// 二叉树右旋
//     P             P      .
//     |             |      .
//     X             Y      .
//    / \    -->    / \     .
//   Y   c         a   X    .
//  / \               / \   .
// a   b             b   c  .
static void rb_rotate_right(rbtree_t *tree, rbnode_t *node) {
    rbnode_t *X = node;
    rbnode_t *Y = X->left;
    rbnode_t *P = RB_PARENT(X);

    if (NULL != (X->left = Y->right)) { // X->left   = b
        set_parent(Y->right, X);        // b->parent = X
    }
    Y->right = X;                       // Y->right  = X
    set_parent(Y, P);                   // Y->parent = P
    set_parent(X, Y);                   // X->parent = Y

    if (NULL == P) {
        tree->root = Y;
    } else if (X == P->left) {
        P->left = Y;
    } else {
        P->right = Y;
    }
}



// 红黑树需要满足的性质：
//  1. 节点要么是红色，要么是黑色
//  2. 根节点是黑色
//  3. 叶节点（NULL）是黑色
//  4. 红节点的两个子节点都是黑色（父子节点不能都红）
//  5. 黑高度相同



// 添加新节点后执行，保持红黑树性质（node 已经添加到红黑树）
static void rb_insert_fixup(rbtree_t *tree, rbnode_t *node) {
    rbnode_t *X = node;
    rbnode_t *P = NULL; // 父节点
    rbnode_t *G = NULL; // 爷节点
    rbnode_t *U = NULL; // 叔节点

    set_color(X, RB_RED); // 新节点染红，不改变黑高度

    while ((P = RB_PARENT(X)) && (RB_RED == RB_COLOR(P))) {
        // P 红，说明 G 黑
        // P、X 都红，违反红黑树性质 4
        G = RB_PARENT(P);
        if (P == G->left) {
            U = G->right;
        } else {
            U = G->left;
        }

        if ((NULL != U) && (RB_RED == RB_COLOR(U))) {
            // P、U 都是红节点
            // 那么 P、U 染黑，G 染红，保持黑高度不变
            // 之后继续检查 G
            set_color(U, RB_BLACK);
            set_color(P, RB_BLACK);
            set_color(G, RB_RED);
            X = G;
            continue;
        }

        // 现在，X、P 红，U 黑，需要将 P 染黑
        // 直接染黑会导致黑高度增加，因此要先旋转再染色

        if (P == G->left) {
            if (X == P->right) {
                // 将两个相邻红节点旋转到左链
                // 小括号 () 表示红色，中括号 [] 表示黑色
                //     [G]           [G]    .
                //     / \           / \    .
                //   (P) [U]  -->  (X) [U]  .
                //     \           /        .
                //     (X)       (P)        .
                rb_rotate_left(tree, P);
                rbnode_t *tmp = X;
                X = P;
                P = tmp;
            }

            // 左侧连续两个红节点，右旋并重新染色
            // 小括号 () 表示红色，中括号 [] 表示黑色
            //     [G]           (P)           [P]      .
            //     / \           / \           / \      .
            //   (P) [U]  -->  (X) [G]  -->  (X) (G)    .
            //   /                   \             \    .
            // (X)                   [U]           [U]  .
            rb_rotate_right(tree, G);
            set_color(P, RB_BLACK);
            set_color(G, RB_RED);
        } else /* if (P == G->right) */ {
            if (X == P->left) {
                // 将两个相邻红节点旋转到右链
                rb_rotate_right(tree, P);
                rbnode_t *tmp = X;
                X = P;
                P = tmp;
            }

            // 右侧连续两个红节点，左旋并重新染色
            rb_rotate_left(tree, G);
            set_color(P, RB_BLACK);
            set_color(G, RB_RED);
        }
    }

    // 确保根节点为黑色
    set_color(tree->root, RB_BLACK);
}

void rb_insert(rbtree_t *tree, rbnode_t *node, rbnode_t *parent, rbnode_t **link) {
    ASSERT(NULL != tree);
    ASSERT(NULL != node);
    ASSERT(NULL != link);

    node->parent_color = (size_t)parent;
    node->left = NULL;
    node->right = NULL;
    *link = node;
    rb_insert_fixup(tree, node);
}

void rb_insert_left(rbtree_t *tree, rbnode_t *node, rbnode_t *parent) {
    rb_insert(tree, node, parent, &parent->left);
}

void rb_insert_right(rbtree_t *tree, rbnode_t *node, rbnode_t *parent) {
    rb_insert(tree, node, parent, &parent->right);
}






// 删除黑节点后执行，保持红黑树性质
// child 是刚刚被删除的节点的唯一子节点，这棵子树黑高度少一
static void rb_remove_fixup(rbtree_t *tree, rbnode_t *child) {
    rbnode_t *X = child;
    rbnode_t *S = NULL; // 兄弟节点
    rbnode_t *L = NULL; // 左侄节点
    rbnode_t *R = NULL; // 左侄节点

    // X 子树黑高度少一，如果 X 是红色，将其染黑即可
    // 如果 X 是黑色，就需要旋转并沿树上移，直到遇到红节点

    while ((X != tree->root) && (RB_BLACK == RB_COLOR(X))) {
        // X 不是根节点，父节点一定非空
        rbnode_t *P = RB_PARENT(X);

        if (X == P->left) {
            // X 是父节点的左子节点，兄弟节点在右
            // X 是黑色，且黑高度少一（不算叶节点），兄弟子树黑高度至少为二
            // 说明兄弟节点一定非空，两个侄节点也一定非空
            S = P->right;

            if (RB_RED == RB_COLOR(S)) {
                // 父节点黑色，否则违反性质 4
                // 兄弟节点染黑，然后左旋，黑高度均不变
                // 这样得到黑色的兄弟节点
                // 小括号 () 表示红色，中括号 [] 表示黑色
                //   [P]           [S]      .
                //   / \           / \      .
                // [X] (S)  -->  (P) [b]    .
                //     / \       / \        .
                //   [a] [b]   [X] [a]      .
                set_color(S, RB_BLACK);
                set_color(P, RB_RED);
                rb_rotate_left(tree, P);
                S = P->right;
            }

            L = S->left;
            R = S->right;

            if ((RB_BLACK == RB_COLOR(L)) && (RB_BLACK == RB_COLOR(R))) {
                // 兄弟节点黑色，且两个侄节点均为黑色
                // 这样兄弟节点可以染红，黑高度也减一
                // 黑高度少一的子树便上升了一层
                //    P             P       .
                //   / \           / \      .
                // [X] [S]  -->  [X] (S)    .
                //     / \           / \    .
                //   [L] [R]       [L] [R]  .
                set_color(S, RB_RED);
                X = P;
            } else {
                if (RB_BLACK == RB_COLOR(R)) {
                    // 右侄节点黑色，重新染色并旋转，使右侄节点为红色
                    //    P             P       .
                    //   / \           / \      .
                    // [X] [S]  -->  [X] [L]    .
                    //     / \           / \    .
                    //   (L) [R]       [a] (S)  .
                    set_color(L, RB_BLACK);
                    set_color(S, RB_RED);
                    rb_rotate_right(tree, S);
                    S = L;
                    L = S->left;
                    R = S->right;
                }

                // 右侄节点红色，重新染色并旋转，整棵红黑树黑高度一致
                //      P             S     .
                //     / \           / \    .
                //   [X] [S]  -->  [P] [R]  .
                //       / \       / \      .
                //      L  (R)   [X]  L     .
                set_color(S, RB_COLOR(P));
                set_color(P, RB_BLACK);
                set_color(R, RB_BLACK);
                rb_rotate_left(tree, P);
                X = tree->root;
                break;
            }
        } else {
            // X 是父节点的右子节点，兄弟节点在左
            // 与左子节点情况完全对称，因此不再说明
            S = P->left;

            if (RB_RED == RB_COLOR(S)) {
                set_color(S, RB_BLACK);
                set_color(P, RB_RED);
                rb_rotate_right(tree, P);
                S = P->left;
            }

            L = S->left;
            R = S->right;

            if ((RB_BLACK == RB_COLOR(L)) && (RB_BLACK == RB_COLOR(R))) {
                set_color(S, RB_RED);
                X = P;
            } else {
                if (RB_BLACK == RB_COLOR(L)) {
                    set_color(R, RB_BLACK);
                    set_color(S, RB_RED);
                    rb_rotate_left(tree, S);
                    S = R;
                    L = S->left;
                    R = S->right;
                }

                set_color(S, RB_COLOR(P));
                set_color(P, RB_BLACK);
                set_color(L, RB_BLACK);
                rb_rotate_right(tree, P);
                X = tree->root;
                break;
            }
        }
    }

    if (NULL != X) {
        set_color(X, RB_BLACK);
    }
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


void rb_remove(rbtree_t *tree, rbnode_t *node) {
    ASSERT(NULL != tree);
    ASSERT(NULL != node);

    rbnode_t *X = NULL; // 被移除的节点（至多有一个子节点）
    rbnode_t *C = NULL; // 需要重新染色，调整平衡性的节点

    if (NULL == node->left) {
        X = node;
        C = node->right;
    } else if (NULL == node->right) {
        X = node;
        C = node->left;
    } else {
        // node 有两个子节点，则 X 为 node 的后继节点（X 只有一个 child）
        // 用 X 替代 node，这样删除具有双子的 node 的问题就转换成了删除只有单子的 X 的问题
        X = node->right;
        while (NULL != X->left) {
            X = X->left;
        }
        C = X->right;
    }

    rbnode_t *P = RB_PARENT(X);
    if (NULL == P) {
        tree->root = C;
    } else if (P->left == X) {
        P->left = C;
    } else {
        P->right = C;
    }
    if (NULL != C) {
        set_parent(C, P);
    }

    size_t color = RB_COLOR(X);

    if (X != node) {
        // 使用 X 顶替 node 在红黑树中的位置
        rb_replace(tree, node, X);
    }

    if (RB_BLACK == color) {
        // 删除了一个黑节点，需要重新染色
        rb_remove_fixup(tree, C);
    }
}






// 节点替换
// victim 是被换出的节点，原本位于红黑树中，换出后属性不变
// node 是被换入的节点，原本不在红黑树中
void rb_replace(rbtree_t *tree, rbnode_t *victim, rbnode_t *node) {
    ASSERT(NULL != tree);
    ASSERT(NULL != victim);
    ASSERT(NULL != node);

    rbnode_t *parent = RB_PARENT(victim);
    if (NULL == parent) {
        tree->root = node;
    } else if (victim == parent->left) {
        parent->left = node;
    } else {
        parent->right = node;
    }

    if (NULL != victim->left) {
        set_parent(victim->left, node);
    }
    if (NULL != victim->right) {
        set_parent(victim->right, node);
    }

    node->parent_color = victim->parent_color;
    node->left         = victim->left;
    node->right        = victim->right;
}
