#ifndef TASK_H
#define TASK_H

#include "kobj.h"
#include "wdog.h"
#include <dllist.h>
#include <vmspace.h>

#define PRIORITY_NUM 32

// 可用于就绪队列/阻塞队列
typedef struct prioq {
    dlnode_t   *heads[PRIORITY_NUM];
    uint32_t    priorities; // mask
} prioq_t;

// 任务状态掩码
enum task_state {
    TS_READY   = 0,
    TS_STOPPED = 1,
    TS_PENDING = 2, // dl 位于某个阻塞队列中
    TS_DELETED = 4, // 资源已经回收，TCB 可以复用
};

typedef struct process proc_t;

// 调度对象，调度器视角下操作的对象
// 这是内核线程所需的最小信息
// 类似于 Linux kernel struct sched_entity
typedef struct task {
    size_t      stack_top;  // regs_t
    size_t      stack0;     // stack_top when syscall
    size_t      stack3;     // saved by syscall
    dlnode_t    dl;         // node in ready-queue OR wait-queue
    _Atomic uint32_t state;
    int16_t     affinity;
    int16_t     priority;

    // 阻塞相关字段：仅当 state 含 TS_PENDING 时有效
    // 由该任务所阻塞的 waitq 所属对象的锁保护（也就是 wait_lock）
    wdog_t      timer;      // 超时定时器，触发后调用 task_timeout
    spinlock_t *wait_lock;  // waitq 所在对象的锁，确认 wdog 删除之后再清除
    prioq_t    *wait_wq;    // 所在的阻塞队列，不在阻塞队列则取值 NULL（guarded by wait_lock）
    int         got;        // 是否被正常唤醒（非超时），阻塞恢复之后读取
    int         expired;    // 是否因超时被唤醒（TODO 未使用）

    // 所属进程的资源
    size_t      pgtbl;      // 就是 process->vm.table，放在这里便于访问
    proc_t  *process;    // parent process (NULL if kernel thread)
    vmrange_t   stack;      // kernel stack
    vmrange_t   user_stack; // user stack

    // 等待线程退出的阻塞队列
    spinlock_t  join_lock;
    prioq_t     join_q;     // guarded by join_lock
} task_t;

void prioq_init(prioq_t *q);
void cpu_preempt_disable();
void cpu_preempt_restore();
task_t *current_task();

INIT_TEXT void sched_init();
void sched_process();

task_t *task_make(const char *name, int prio, void *func);

// 阻塞当前任务，将其放入 waitq，可选地启动超时定时器
// 调用者必须持有 `lock`（即 waitq 所属对象的锁），中断关闭
// `lock` 会被记录到 TCB，供超时回调使用
void task_pend(prioq_t *wq, spinlock_t *lock, int timeout);

// 恢复一个任务的运行需要分两步：
// 1. 持有对象锁，取出 waitq 里面的一个任务
// 2. 不持有锁，等待这个任务的恢复，删除这个任务的 wdog
task_t *task_unpend_one_nolock(prioq_t *wq);
void task_unpend_finish(task_t *tid);

void task_exit();

void task_start_now(task_t *tid);
uint64_t task_start(task_t *tid);
void notify_resched(uint64_t cpumask);

void task_join_and_drop(task_t *tid);
void task_drop(task_t *tid);

#endif // TASK_H
