#ifndef SCHED_H
#define SCHED_H

#include <library/dllist.h>
#include <library/spin.h>
#include <memory/vmspace.h>
#include "work.h"

// 任务状态，多个状态可以并存
typedef enum task_state {
    TASK_READY   = 0,
    TASK_PENDING = 1,   // 因为 semaphore、pipe、msgqueue 等资源而阻塞，处于它们的等待队列中
    TASK_STOPPED = 2,   // 被手动暂停，不在就绪队列中
    TASK_WAITING = 4,   // 等待固定时间
    TASK_DELETED = 8,   // 已停止，即将删除（已预约下一次 work_queue）
} task_state_t;

// 代表一个线程
typedef struct task {
    void       *arch_regs;  // 指向中断栈上保存的上下文，中断时的栈顶
    spin_t      spin;
    dlnode_t    q_node;     // 就绪队列/阻塞队列节点
    uint8_t     state;
    uint8_t     priority;   // 最大取值 31，可以换成 bitfield
    int         tick;
    int         tick_reload;
    vmrange_t   stack;
    work_t      work;       // 异步操作，如任务退出、阻塞超时
} task_t;


extern PERCPU_BSS task_t *g_tid_prev;
extern PERCPU_BSS task_t *g_tid_next;


task_t *sched_stop(uint16_t bits);
int sched_cont(task_t *tid, uint16_t bits);

void task_create(task_t *tid, const char *name, uint8_t priority, int tick,
        void *entry, void *arg1, void *arg2, void *arg3, void *arg4);

void task_resume(task_t *tid);
void task_sleep(int tick);
void task_exit();

void sched_advance();
INIT_TEXT void sched_init();

#endif // SCHED_H
