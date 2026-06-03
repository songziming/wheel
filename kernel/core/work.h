#ifndef WORK_H
#define WORK_H

#include <dllist.h>

typedef struct work work_t;
typedef void (*work_cb_t)(work_t*);
struct work {
    dlnode_t dl;
    const char *desc;
    work_cb_t   func;
};

INIT_TEXT void work_init_this();
void work_defer(work_t *wk, work_cb_t func, const char *desc);
void work_flush();

#endif // WORK_H
