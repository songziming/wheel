#ifndef WORK_H
#define WORK_H

#include <library/dllist.h>

typedef void (*work_func_t)(void *, void*);

typedef struct work {
    dlnode_t    dl;
    work_func_t func;
    void       *arg1;
    void       *arg2;
} work_t;

INIT_TEXT void work_init();
void work_defer(work_t *work, work_func_t func, void *arg1, void *arg2);
void work_q_flush();

#endif // WORK_H
