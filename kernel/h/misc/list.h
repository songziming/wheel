#ifndef MISC_LIST_H
#define MISC_LIST_H

#include <base.h>

typedef struct dlnode dlnode_t;
typedef struct dllist dllist_t;

struct dlnode {
    dlnode_t * prev;
    dlnode_t * next;
};

struct dllist {
    dlnode_t * head;
    dlnode_t * tail;
};

#define DLNODE_INIT ((dlnode_t) { NULL, NULL })
#define DLLIST_INIT ((dllist_t) { NULL, NULL })

extern void       dl_push_head    (dllist_t * list, dlnode_t * node);
extern void       dl_push_tail    (dllist_t * list, dlnode_t * node);
extern dlnode_t * dl_pop_head     (dllist_t * list);
extern dlnode_t * dl_pop_tail     (dllist_t * list);
extern void       dl_insert_before(dllist_t * list, dlnode_t * x, dlnode_t * y);
extern void       dl_insert_after (dllist_t * list, dlnode_t * x, dlnode_t * y);
extern void       dl_remove       (dllist_t * list, dlnode_t * node);
extern int        dl_is_empty     (dllist_t * list);

#endif // MISC_LIST_H
