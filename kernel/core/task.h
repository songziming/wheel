#ifndef TASK_H
#define TASK_H

#include "wdog.h"
#include <dllist.h>
#include <vmspace.h>

#define PRIORITY_NUM 32

// 可用于就绪队列/阻塞队列
typedef struct prioq {
    dlnode_t   *heads[PRIORITY_NUM];
    uint32_t    priorities; // mask
} prioq_t;

// 阻塞队列节点
typedef struct waiter {
    dlnode_t    dl;
    task_t     *tid; // 阻塞的任务
    wdog_t      timer;
    void       *user;
    int         got;    // 是否正常唤醒
    int         expired; // 是否已超时
} waiter_t;

// 任务状态掩码
enum task_state {
    TS_READY   = 0,
    TS_STOPPED = 1,
    TS_PENDING = 2, // dl 位于某个阻塞队列中
    TS_DELETED = 4, // 资源已经回收，TCB 可以复用
};

typedef struct task {
    void       *stack_top;  // regs_t
    dlnode_t    objnode;    // node in tasklist
    dlnode_t    dl;         // node in ready-queue
    uint32_t    state;
    int16_t     affinity;
    int16_t     priority;
    const char *name;
    vmrange_t   stack;
    lockdep_task_t lockdep;
} task_t;

void prioq_init(prioq_t *q);
void preempt_lock();
void preempt_unlock();
task_t *current_task();

INIT_TEXT void sched_init();
void sched_process();

void task_create(task_t *tid, const char *name, int prio, void *func);

void task_pend(prioq_t *wq, waiter_t *pender, int timeout, wdog_cb_t cb);
task_t *task_unpend_one(prioq_t *wq);
void task_wake_timeout(prioq_t *wq, waiter_t *pender);
void task_onresume(waiter_t *pender);

void task_exit();

void task_start_now(task_t *tid);
uint64_t task_start(task_t *tid);
void notify_resched(uint64_t cpumask);

#endif // TASK_H
