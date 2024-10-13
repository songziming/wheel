// 信号量，一个共享整型变量，表示可用资源数量，支持两种操作：
// - take，减小取值，若当前取值不足，则当前任务阻塞
// - give，增加取值，唤醒被阻塞的任务
// 信号量可以简化为互斥锁（mutex），不少 OS 只提供 mutex，不提供 semaphore

#include "semaphore.h"
// #include <wheel.h>
#include <library/debug.h>
#include <proc/tick.h>
#include <proc/sched.h>
#include <arch_intf.h>



// 代表一个阻塞的任务，保存在栈上
typedef struct pend_item {
    dlnode_t  dl;
    task_t   *task;
    int       require;
    int       got;
} pend_item_t;



//------------------------------------------------------------------------------
// 初始化信号量
//------------------------------------------------------------------------------

void semaphore_init(semaphore_t *sem, int initial, int max) {
    ASSERT(NULL != sem);
    ASSERT(initial >= 0);
    ASSERT(max > 0);

    if (initial > max) {
        initial = max;
    }

    // sem->spin = SPIN_INIT;
    spin_init(&sem->spin);
    sem->limit = max;
    sem->value = initial;
    sched_list_init(&sem->penders);
}


//------------------------------------------------------------------------------
// 超时处理函数，在中断里执行
//------------------------------------------------------------------------------

// 尚未得到信号量，但等待时间已过
static void semaphore_wakeup(void *arg1, void *arg2) {
    semaphore_t *sem = (semaphore_t *)arg1;
    pend_item_t *item = (pend_item_t *)arg2;

    ASSERT(cpu_int_depth());
    ASSERT(NULL != sem);
    ASSERT(NULL != item);

    int key = irq_spin_take(&sem->spin);
    task_t *task = item->task;

    // 还有一种可能：超时 timer 在 CPU0 执行，同时另一个 CPU 将阻塞的任务恢复
    // 因此锁住信号量之后，首先要检查 pend_item 还在不在队列中
    // 而且 pend_item 是临时变量，如果动作慢了可能被恢复运行的 task 释放

    // 这段代码可能与其他 CPU 竞争恢复目标任务
    // 可能与恢复运行的目标任务竞争 pend_item 的访问

    if (sched_list_contains(&sem->penders, task->priority, &item->dl)) {
        irq_spin_give(&sem->spin, key);
        return;
    }
    sched_list_remove(&sem->penders, task->priority, &item->dl);

    int cpu = sched_cont(task, TASK_PENDING); // 恢复任务
    irq_spin_give(&sem->spin, key);

    // 如果任务在当前 CPU 恢复，则无需切换任务，因为目前就处在中断
    if ((cpu >= 0) && (cpu_index() != cpu)) {
        arch_ipi_resched(cpu);
    }
}


//------------------------------------------------------------------------------
// 获取信号量，可能阻塞，不能在中断内调用
//------------------------------------------------------------------------------

// 如果成功获得信号量，则返回非零
// 如果获取信号量失败（例如超时、信号量被删除），则返回零

// 成功则返回 n，失败返回 0，总之返回实际得到的资源数

int semaphore_take(semaphore_t *sem, int n, int timeout) {
    ASSERT(0 == cpu_int_depth());
    ASSERT(NULL != sem);
    ASSERT(n > 0);

    // 锁住中断，避免当前任务的执行被打断
    int key = irq_spin_take(&sem->spin);

    // 如果资源足够，可以直接返回，不用阻塞
    if (sem->value >= n) {
        sem->value -= n;
        irq_spin_give(&sem->spin, key);
        return n;
    }

    if (NOWAIT == timeout) {
        return 0;
    }

    // 当前任务变为阻塞态，但开启中断前还能保持运行
    task_t *self = sched_stop(TASK_PENDING);

    // 创建一个 pender，添加到阻塞队列
    pend_item_t item;
    item.task = self;
    item.require = n;
    item.got = 0;
    sched_list_insert(&sem->penders, self->priority, &item.dl);

    // 指定了超时时间，则开启一个 timer
    timer_t wakeup;
    if (FOREVER != timeout) {
        timer_start(&wakeup, timeout, semaphore_wakeup, &sem, &item);
    }

    // 放开锁，切换到其他任务
    irq_spin_give(&sem->spin, key);
    arch_task_switch();

    // 重新恢复运行，把计时器停止
    if (FOREVER != timeout) {
        timer_cancel(&wakeup);
    }

    // 检查原因（因为得到了信号量，还是因为超时）
    return item.got;
}


//------------------------------------------------------------------------------
// 释放信号量，将阻塞的任务恢复，可以在任务和中断里执行
//------------------------------------------------------------------------------

int fifo_semaphore_give(fifo_semaphore_t *sem, int n) {
    ASSERT(NULL != sem);
    ASSERT(n > 0);

    semaphore_t *common = &sem->common;
    dlnode_t *pend_q = &sem->penders;

    int key = irq_spin_take(&common->spin);

    common->value += n;
    if (common->value > common->limit) {
        n -= common->value - common->limit;
        common->value = common->limit;
    }

    cpuset_t resched_mask = 0;
    while ((common->value > 0) && !dl_is_lastone(pend_q)) {
        pend_item_t *item = containerof(pend_q->next, pend_item_t, dl);
        if (common->value < item->require) {
            continue;
        }

        dl_remove(&item->dl);
        common->value -= item->require;
        item->got = item->require;

        task_t *task = item->task;
        int cpu = sched_cont(task, TASK_PENDING); // 恢复任务
        if (cpu >= 0) {
            resched_mask |= 1UL << cpu;
        }
    }

    irq_spin_give(&common->spin, key);
    notify_resched(resched_mask);

    return n;
}

int semaphore_give(semaphore_t *sem, int n) {
    ASSERT(NULL != sem);
    ASSERT(n > 0);

    // semaphore_t *sem = &sem->sem;
    sched_list_t *pend_q = &sem->penders;

    int key = irq_spin_take(&sem->spin);

    sem->value += n;
    if (sem->value > sem->limit) {
        n -= sem->value - sem->limit;
        sem->value = sem->limit;
    }

    cpuset_t resched_mask = 0;
    while (sem->value > 0) {
        dlnode_t *head = sched_list_head(pend_q);
        if (NULL == head) {
            break;
        }

        pend_item_t *item = containerof(head, pend_item_t, dl);
        if (sem->value < item->require) {
            continue;
        }

        task_t *task = item->task;
        sched_list_remove(pend_q, task->priority, &item->dl);
        sem->value -= item->require;
        item->got = item->require;

        int cpu = sched_cont(task, TASK_PENDING); // 恢复任务
        if (cpu >= 0) {
            resched_mask |= 1UL << cpu;
        }
    }

    irq_spin_give(&sem->spin, key);
    notify_resched(resched_mask);

    return n;
}
