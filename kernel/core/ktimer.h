#ifndef KTIMER_H
#define KTIMER_H

#include <dllist.h>

// 代表一个定时任务
typedef struct ktimer ktimer_t;
struct ktimer {
    dlnode_t    dl;
    int         delta;  // 和前一个 job 相差多少个 tick
    void (*func)(ktimer_t *self);
};

INIT_TEXT void timer_init();
void timer_process();
void timer_start(ktimer_t *job, int tick);
void timer_cancel(ktimer_t *job);

#endif // KTIMER_H
