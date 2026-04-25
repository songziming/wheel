#include "semaphore.h"
#include "task.h"
#include <debug.h>


// 代表 semaphore 阻塞队列中的元素
// 由阻塞的任务在栈上创建
typedef struct pender {
    dlnode_t dl;
    task_t  *task;
    int      require;
} pender_t;

void semaphore_init(semaphore_t *sem, int initial, int max) {
    // spin_init(&sem->lock);
    sem->lock = SPIN_INIT;
    sem->limit = max;
    sem->value = initial;
    dl_init_circular(&sem->penders);
}

// value -= n，返回获取到的资源数量
// 如果减小之后 value 小于 0，则任务阻塞
// 等待其他任务释放信号量，使得 value-n 非负，则当前任务恢复运行
int semaphore_take(semaphore_t *sem, int n, int timeout) {
    int key = irq_spin_take(&sem->lock);

    // 资源足够丰富，无需阻塞立即返回
    if (sem->value >= n) {
        sem->value -= n;
        irq_spin_give(&sem->lock, key);
        return n;
    }

    // 如果不等待，则立即返回失败
    if (NOWAIT == timeout) {
        return 0;
    }

    // 栈上创建一个 pend-q 元素，放入阻塞队列
    pender_t pender;
    pender.task = sched_stop_self(TS_PENDING);
    pender.require = n;
    dl_insert_before(&pender.dl, &sem->penders);

    // TODO 如果有超时时间，还应该设定一个 ktimer

    // 释放锁，切换到其他任务
    irq_spin_give(&sem->lock, key);
    arch_task_switch();

    // 如果从 arch_task_switch 返回，说明已经得到信号量资源
    // logk("wake up from semaphore pending\n");
    return n;
}

void semaphore_give(semaphore_t *sem, int n) {
    int key = irq_spin_take(&sem->lock);

    sem->value += n;
    if (sem->value > sem->limit) {
        n -= sem->value - sem->limit;
        sem->value = sem->limit;
    }

    // 从前向后检查阻塞队列，看有无可以恢复的阻塞者
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
        sched_cont(pender->task, TS_PENDING); // TODO 选择负载最轻的 CPU
    }

    irq_spin_give(&sem->lock, key);

    // TODO 检查哪些CPU启动了新的任务，通知它们
    arch_task_switch();
}
