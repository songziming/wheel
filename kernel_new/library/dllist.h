#ifndef DLLIST_H
#define DLLIST_H

#include <common.h>

typedef struct dlnode {
    struct dlnode *prev;
    struct dlnode *next;
} dlnode_t;

dlnode_t *dl_init_circular(dlnode_t *node);
int dl_is_lastone(dlnode_t *node);
int dl_contains(dlnode_t *head, dlnode_t *node);

void dl_insert_before(dlnode_t *node, dlnode_t *ref);
void dl_insert_after(dlnode_t *node, dlnode_t *ref);

dlnode_t *dl_remove(dlnode_t *node);

#endif // DLLIST_H
