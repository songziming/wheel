#include "mutex.h"
#include <debug.h>

// 二值的信号量，但是更严格，必须由相同的线程获取/释放
// 不能跨线程获取/释放，也不能在 ISR 里面使用

void mutex_init(mutex_t *mut) {
    prioq_init(&mut->wq);
    mut->owner = NULL;
}

int mutex_take(mutex_t *mut, int timeout) {
    ASSERT(0 == cpu_int_depth());

    task_t *self = THISCPU_GET(g_tid_prev);

    int key = irq_spin_take(&mut->wq.lock);
    ASSERT(self != mut->owner); // 不许重入
    if (NULL == mut->owner) {
        mut->owner = self;
        irq_spin_give(&mut->wq.lock, key);
        return 1;
    }

    if (NOWAIT == timeout) {
        irq_spin_give(&mut->wq.lock, key);
        return 0;
    }

    // 没有得到，需要阻塞
    return !task_stop(TS_PENDING, &mut->wq, key, timeout);
}

void mutex_give(mutex_t *mut) {
    ASSERT(0 == cpu_int_depth());

    int key = irq_spin_take(&mut->wq.lock);
    task_t *self = THISCPU_GET(g_tid_prev);
    if (self != mut->owner) {
        panic("release mutex from %p, owner=%p\n", self, mut->owner);
    }

    // 尝试唤醒一个阻塞线程
    dlnode_t *dl = prioq_head_nolock(&mut->wq);
    if (dl) {
        // 存在阻塞者，将其唤醒，并将其设为新的 owner
        waiter_t *w = containerof(dl, waiter_t, dl);
        mut->owner = w->tid;
        prioq_remove_nolock(&mut->wq, dl, w->tid->priority);
        int cpu = task_cont(w->tid, TS_PENDING);
        irq_spin_give(&mut->wq.lock, key);

        // 发送 IPI，或者立即切换任务
        if (cpu >= 0) {
            if (cpu_index() != cpu) {
                arch_send_ipi(cpu, VEC_IPI_RESCHED);
            } else {
                arch_task_switch();
            }
        }
        return;
    }

    // 没有阻塞者，mutex 转换为 unlocked 状态
    mut->owner = NULL;
    irq_spin_give(&mut->wq.lock, key);
}
