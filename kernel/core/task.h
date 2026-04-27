#ifndef CORE_TASK_H
#define CORE_TASK_H

#include <arch_api.h>
#include <vmspace.h>
#include <dllist.h>
#include <spin.h>


// 就绪队列（也可以作为阻塞队列？）
typedef struct rdyq {
    spin_t      lock;
    uint32_t    priorities; // mask
    dlnode_t   *heads[32];
} rdyq_t;


// 任务状态掩码
enum task_state {
    TS_READY   = 0,
    TS_STOPPED = 1,
    TS_PENDING = 2, // dl 位于某个阻塞队列中
};


// 代表一个任务，也是调度的基本单位
typedef struct task {
    void       *regs;
    const char *name;
    dlnode_t    dl;
    int         priority;   // TODO 可以用 bitfield 压缩
    uint32_t    state;      // TODO 可以用 bitfield 压缩
    vmrange_t   stack;
    int         tick;
    int         tick_reload;
} task_t;


INIT_TEXT void rdyq_init(rdyq_t *q);
void rdyq_insert(rdyq_t *q, dlnode_t *dl, int prio);
void rdyq_remove(rdyq_t *q, dlnode_t *dl, int prio);
dlnode_t *rdyq_head(rdyq_t *q);
// dlnode_t *rdyq_rotate(rdyq_t *q, dlnode_t *dl);

INIT_TEXT void sched_init();
void sched_process();
// void sched_stop(task_t *task, uint32_t bits);
task_t *sched_stop_self(uint32_t bits);
void sched_cont(task_t *task, uint32_t bits);

void task_create(task_t *task, const char *name, int priority, void *entry);
void task_start(task_t *task);

#endif // CORE_TASK_H
