#ifndef TASK_H
#define TASK_H

// #include <wheel.h>
#include "ktimer.h"
#include <dllist.h>
#include <vmspace.h>

#define PRIORITY_NUM 32

// 可用于就绪队列/阻塞队列
typedef struct prioq {
    spin_t      lock;
    dlnode_t   *heads[PRIORITY_NUM];
    uint32_t    priorities; // mask
} prioq_t;

// 阻塞队列节点
typedef struct waiter {
    dlnode_t    dl;
    prioq_t    *wq; // 阻塞在哪个队列
    task_t     *tid; // 阻塞的任务
    ktimer_t    timer;
} waiter_t;

// 任务状态掩码
enum task_state {
    TS_READY   = 0,
    TS_STOPPED = 1,
    TS_PENDING = 2, // dl 位于某个阻塞队列中
    TS_DELETED = 3, // 资源已经回收，TCB 可以复用
};

typedef struct task {
    void       *stack_top;  // regs_t
    dlnode_t    dl;         // node in ready-queue
    uint32_t    state;
    const char *name;
    int         priority;
    int         affinity;
    vmrange_t   stack;
} task_t;

extern task_t *g_tid_prev;
extern task_t *g_tid_next;

void prioq_init(prioq_t *q);
void prioq_insert_nolock(prioq_t *q, dlnode_t *dl, int prio);
void prioq_remove_nolock(prioq_t *q, dlnode_t *dl, int prio);
int prioq_contains_nolock(prioq_t *q, dlnode_t *dl, int prio);
dlnode_t *prioq_head_nolock(prioq_t *q);

void preempt_lock();
void preempt_unlock();

INIT_TEXT void sched_init();
void sched_process();

void task_create(task_t *tid, const char *name, int prio, void *func);
void task_stop(uint32_t bits, prioq_t *wq, int timeout);
void task_start_one(task_t *tid);
uint64_t task_start(task_t *tid);
void notify_resched(uint64_t cpumask);

#endif // TASK_H
