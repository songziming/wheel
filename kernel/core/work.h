#ifndef WORK_H
#define WORK_H

#include <dllist.h>

typedef struct work work_t;

struct work {
    dlnode_t dl;
    void (*func)(work_t *self);
};

INIT_TEXT void work_init_this();
void work_defer(work_t *wk, void *func);
void work_flush();

#endif // WORK_H
