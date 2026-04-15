#ifndef CORE_TASK_H
#define CORE_TASK_H

#include <arch_api.h>
#include <dllist.h>

// 代表一个定时任务
typedef struct timerjob timerjob_t;
struct timerjob {
    dlnode_t    dl;
    int         delta;  // 和前一个 job 相差多少个 tick
    void (*func)(timerjob_t *self);
};

// 代表一个任务，也是调度的基本单位
typedef struct task {
    void       *regs;
    const char *name;
} task_t;

// extern task_t *g_prev_task;
// extern task_t *g_next_task;

INIT_TEXT void timer_init();
void timer_process();
void timer_start(timerjob_t *job, int tick);
void timer_cancel(timerjob_t *job);

INIT_TEXT void sched_init();
void sched_process();

void task_create(task_t *task, const char *name, void *entry, void *stack_stop);

#endif // CORE_TASK_H
