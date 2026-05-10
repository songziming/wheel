#ifndef TASK_H
#define TASK_H

#include <wheel.h>
#include <dllist.h>
#include <vmspace.h>

// 任务状态掩码
enum task_state {
    TS_READY   = 0,
    TS_STOPPED = 1,
    TS_PENDING = 2, // dl 位于某个阻塞队列中
    TS_DELETED = 3, // 资源已经回收，TCB 可以复用
};

typedef struct task {
    void       *stack_top; // regs_t
    dlnode_t    dl;
    uint32_t    state;
    const char *name;
    int         priority;
    int         affinity;
    vmrange_t   stack;
} task_t;

extern task_t *g_tid_prev;
extern task_t *g_tid_next;

INIT_TEXT void sched_init();
void sched_process();

void task_create(task_t *tid, const char *name, int prio, void *func);
void task_stop(uint32_t bits);
uint64_t task_start(task_t *tid);

#endif // TASK_H
