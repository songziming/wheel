#ifndef TICK_H
#define TICK_H

#include <dllist.h>

typedef void (*work_func_t)(void *arg);

typedef struct work {
    dlnode_t    node;
    int         tick;
    work_func_t func;
    void       *arg;
} work_t;

#define WORK_INIT ((work_t){ DLNODE_INIT,0,NULL,NULL })


void tick_delay(work_t *work, int tick, work_func_t func, void *arg);

void work_defer(work_t *work, work_func_t func, void *arg);
void work_q_flush();

INIT_TEXT void tick_init();
size_t tick_get();
void tick_advance();

#endif // TICK_H