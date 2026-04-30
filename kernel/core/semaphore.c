#include "semaphore.h"
#include "task.h"
#include <ktimer.h>
#include <debug.h>


// 代表 semaphore 阻塞队列中的元素
// 由阻塞的任务在栈上创建
typedef struct pender {
    dlnode_t     dl;
    task_t      *task;
    semaphore_t *sem;
    int          require;
    int          timedout;
    ktimer_t     timer;
} pender_t;

// 超时回调：从阻塞队列移除任务，唤醒
static void semaphore_timeout(ktimer_t *tmr) {
    pender_t *pender = containerof(tmr, pender_t, timer);
    semaphore_t *sem = pender->sem;

    int key = irq_spin_take(&sem->lock);

    // semaphore_give 可能已经移除了 pender
    if (dl_contains(&sem->penders, &pender->dl)) {
        dl_remove(&pender->dl);
        pender->timedout = 1;
        if (pender->task->affinity < 0) {
            sched_cont(pender->task, TS_PENDING);
        } else {
            sched_cont_on(pender->task, TS_PENDING, pender->task->affinity);
            arch_send_ipi(pender->task->affinity, VEC_IPI_RESCHED);
        }
    }

    irq_spin_give(&sem->lock, key);
}

void semaphore_init(semaphore_t *sem, int initial, int max) {
    // spin_init(&sem->lock);
    sem->lock = SPIN_INIT;
    sem->limit = max;
    sem->value = initial;
    dl_init_circular(&sem->penders);
}

// value -= n，返回获取到的资源数量
// 如果减小之后 value 小于 0，则任务阻塞。
// timeout > 0: 最多等待 timeout 个 tick
// timeout == NOWAIT: 不等待立即返回
// 其他: 永久等待
// 不能在 ISR 运行
int semaphore_take(semaphore_t *sem, int n, int timeout) {
    ASSERT(0 == cpu_int_depth());
    int key = irq_spin_take(&sem->lock);

    if (sem->value >= n) {
        sem->value -= n;
        irq_spin_give(&sem->lock, key);
        return n;
    }

    if (NOWAIT == timeout) {
        irq_spin_give(&sem->lock, key);
        return 0;
    }

    pender_t pender;
    pender.task = sched_stop_self(TS_PENDING);
    pender.sem = sem;
    pender.require = n;
    pender.timedout = 0;
    dl_insert_before(&pender.dl, &sem->penders);

    if (timeout > 0) {
        timer_start(&pender.timer, semaphore_timeout, timeout);
    }

    irq_spin_give(&sem->lock, key);
    arch_task_switch();

    if (timeout > 0) {
        timer_cancel(&pender.timer);
    }

    return pender.timedout ? 0 : n;
}

// 可以在 ISR 执行
void semaphore_give(semaphore_t *sem, int n) {
    int key = irq_spin_take(&sem->lock);

    sem->value += n;
    if (sem->value > sem->limit) {
        n -= sem->value - sem->limit;
        sem->value = sem->limit;
    }

    // 从前向后检查阻塞队列，看有无可以恢复的阻塞者
    uint64_t ipi_mask = 0;
    dlnode_t *dl = sem->penders.next;
    while ((sem->value > 0) && (dl != &sem->penders)) {
        pender_t *pender = containerof(dl, pender_t, dl);
        dl = dl->next;
        if (pender->require > sem->value) {
            continue;
        }

        // 这个任务可以恢复运行
        sem->value -= pender->require;
        dl_remove(&pender->dl);
        if (pender->task->affinity < 0) {
            sched_cont(pender->task, TS_PENDING);
        } else {
            sched_cont_on(pender->task, TS_PENDING, pender->task->affinity);
            ipi_mask |= 1ULL << pender->task->affinity;
        }
    }

    irq_spin_give(&sem->lock, key);

    while (ipi_mask) {
        int cpu = __builtin_ctzll(ipi_mask);
        ipi_mask &= ipi_mask - 1;
        arch_send_ipi(cpu, VEC_IPI_RESCHED);
    }
    arch_task_switch();
}
