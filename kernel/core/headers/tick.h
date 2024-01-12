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

INIT_TEXT void work_init();

void work_delay(work_t *work, int tick, work_func_t func, void *arg);
void work_cancel(work_t *work);

size_t tick_get();
void tick_advance();

#endif // TICK_H