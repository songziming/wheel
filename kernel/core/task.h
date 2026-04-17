#ifndef CORE_TASK_H
#define CORE_TASK_H

#include <arch_api.h>
#include <vmspace.h>
#include <dllist.h>

// 代表一个定时任务
typedef struct timerjob timerjob_t;
struct timerjob {
    dlnode_t    dl;
    int         delta;  // 和前一个 job 相差多少个 tick
    void (*func)(timerjob_t *self);
};


// 就绪队列（也可以作为阻塞队列？）
typedef struct rdyq {
    uint32_t    priorities; // mask
    dlnode_t   *heads[32];
} rdyq_t;


// 任务状态掩码
typedef enum task_state {
    TS_READY   = 0,
    TS_STOPPED = 1,
    TS_PENDING = 2, // dl 位于某个阻塞队列中
} task_state_t;


// 代表一个任务，也是调度的基本单位
typedef struct task {
    void           *regs;
    const char     *name;
    dlnode_t        dl;
    int             priority;
    task_state_t    state;
    vmrange_t       stack;
} task_t;


INIT_TEXT void timer_init();
void timer_process();
void timer_start(timerjob_t *job, int tick);
void timer_cancel(timerjob_t *job);

INIT_TEXT void rdyq_init(rdyq_t *q);
void rdyq_insert(rdyq_t *q, dlnode_t *dl, int prio);
void rdyq_remove(rdyq_t *q, dlnode_t *dl, int prio);
dlnode_t *rdyq_head(rdyq_t *q);
dlnode_t *rdyq_rotate(rdyq_t *q, dlnode_t *dl);

INIT_TEXT void sched_init();
void sched_process();
void sched_stop(task_t *task, task_state_t bits);
void sched_resume_at(task_t *task, int cpu);
void sched_resume(task_t *task);

void task_create_ex(task_t *task, const char *name, int priority, size_t stack_top, void *entry);
void task_create(task_t *task, const char *name, int priority, void *entry);

#endif // CORE_TASK_H
