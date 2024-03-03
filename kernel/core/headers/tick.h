#ifndef TICK_H
#define TICK_H

#include <dllist.h>

typedef void (*work_func_t)(void *a1, void *a2);

typedef struct work {
    dlnode_t    node;
    int         tick;
    work_func_t func;
    void       *arg1;
    void       *arg2;
} work_t;

#define WORK_INIT ((work_t){ DLNODE_INIT,0,NULL,NULL,NULL })

// TODO 应该把 tick/work 区分开

INIT_TEXT void tick_init();
size_t tick_get();
void tick_delay(work_t *work, int tick, work_func_t func, void *arg1, void *arg2);
void work_cancel(work_t *work);
void tick_advance();

INIT_TEXT void work_init();
void work_defer(work_t *work, work_func_t func, void *arg1, void *arg2);
void work_q_flush();

#endif // TICK_H