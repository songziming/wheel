#ifndef RBTREE_H
#define RBTREE_H

#include <common.h>

typedef struct rbnode {
    size_t          parent_color;   // 最低 bit 表示颜色
    struct rbnode  *left;
    struct rbnode  *right;
} ALIGNED(8) rbnode_t;

typedef struct rbtree {
    rbnode_t *root;
} rbtree_t;

#define RBNODE_INIT ((rbnode_t){ 0, NULL, NULL })
#define RBTREE_INIT ((rbtree_t){ NULL })

#define RB_RED      0
#define RB_BLACK    1
#define RB_COLOR(node)  ((size_t)    ((node)->parent_color &  1UL))
#define RB_PARENT(node) ((rbnode_t *)((node)->parent_color & ~1UL))

void rb_insert(rbtree_t *tree, rbnode_t *node, rbnode_t *parent, rbnode_t **link);
void rb_insert_left(rbtree_t *tree, rbnode_t *node, rbnode_t *parent);
void rb_insert_right(rbtree_t *tree, rbnode_t *node, rbnode_t *parent);

void rb_remove(rbtree_t *tree, rbnode_t *node);

void rb_replace(rbtree_t *tree, rbnode_t *victim, rbnode_t *node);

#endif // RBTREE_H
