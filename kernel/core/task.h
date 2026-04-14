#ifndef CORE_TASK_H
#define CORE_TASK_H

#include <wheel.h>
#include <dllist.h>

// 代表一个定时任务
typedef struct timerjob timerjob_t;
struct timerjob {
    dlnode_t    dl;
    int         delta;  // 和前一个 job 相差多少个 tick
    void (*func)(timerjob_t *self);
};

INIT_TEXT void timer_init();
void timer_forward();
void timer_start(timerjob_t *job, int tick);
void timer_cancel(timerjob_t *job);

void tick_advance();

#endif // CORE_TASK_H
