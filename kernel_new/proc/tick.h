#ifndef TICK_H
#define TICK_H

#include <library/dllist.h>

typedef void (*timer_func_t)(void *, void*);

typedef struct timer {
    dlnode_t    dl;
    int         delta;  // 和前一个 timer 相差多少个 tick
    timer_func_t func;
    void       *arg1;
    void       *arg2;
} timer_t;

void timer_start(timer_t *timer, int tick, timer_func_t func, void *arg1, void *arg2);
void timer_cancel(timer_t *timer);
void timer_cancel_sync(timer_t *timer);


INIT_TEXT void tick_init();
size_t tick();
void tick_advance();

#endif // TICK_H
