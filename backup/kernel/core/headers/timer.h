#ifndef TIMER_H
#define TIMER_H

#include <def.h>
#include <dllist.h>

typedef uint64_t tick_t;
#define NOWAIT  0
#define FOREVER UINT64_MAX

typedef void (*timer_func_t)(void *a1, void *a2);

typedef struct timer {
    dlnode_t    dl;
    tick_t      delta;  // 和前一个 timer 相差多少个 tick

    timer_func_t func;
    void       *arg1;
    void       *arg2;
} timer_t;


INIT_TEXT void timer_lib_init();
void timer_proceed();

void timer_start(timer_t *timer, tick_t tick, timer_func_t func, void *a1, void *a2);
void timer_cancel(timer_t *timer);
void timer_cancel_sync(timer_t *timer);

#endif // TIMER_H
