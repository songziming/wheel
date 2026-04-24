#ifndef KTIMER_H
#define KTIMER_H

#include <dllist.h>

// TODO 可以改名为 wdog，借鉴 vxworks

// 代表一个定时任务
typedef struct ktimer ktimer_t;
typedef void (*timer_func_t)(ktimer_t *self);
struct ktimer {
    dlnode_t    dl;
    int         delta;  // 和前一个 job 相差多少个 tick
    timer_func_t    func;
};

INIT_TEXT void timer_init();
void timer_process();
void timer_start(ktimer_t *job, timer_func_t func, int tick);
void timer_cancel(ktimer_t *job);

#endif // KTIMER_H
