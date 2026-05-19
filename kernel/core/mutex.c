#include "mutex.h"
#include <debug.h>

// 二值的信号量，但是更严格，必须由相同的线程获取/释放
// 不能跨线程获取/释放，也不能在 ISR 里面使用

void mutex_init(mutex_t *mut) {
    mut->lock = SPIN_INIT;
    prioq_init(&mut->wq);
    mut->owner = NULL;
}

static void mutex_timeout(wdog_t *tmr) {
    waiter_t *waiter = containerof(tmr, waiter_t, timer);
    // mutex_t *mut = containerof(waiter->wq, mutex_t, wq);
    mutex_t *mut = (mutex_t*)waiter->user;
    raw_spin_take(&mut->lock);
    task_wake_timeout(&mut->wq, waiter);
    raw_spin_give(&mut->lock);
}

int mutex_take(mutex_t *mut, int timeout) {
    ASSERT(0 == cpu_int_depth());

    task_t *self = THISCPU_GET(g_tid_prev);

    int key = irq_spin_take(&mut->lock);
    ASSERT(self != mut->owner); // 不许重入
    if (NULL == mut->owner) {
        mut->owner = self;
        irq_spin_give(&mut->lock, key);
        return 1;
    }

    if (NOWAIT == timeout) {
        irq_spin_give(&mut->lock, key);
        return 0;
    }

    // 没有得到，需要阻塞
    waiter_t pender;
    pender.user = mut;
    task_pend(TS_PENDING, &mut->wq, &pender, timeout, mutex_timeout);
    irq_spin_give(&mut->lock, key);
    arch_task_switch();

    // 恢复运行，检查是否因为超时而唤醒
    return !task_onresume(&pender);
}

void mutex_give(mutex_t *mut) {
    ASSERT(0 == cpu_int_depth());

    int key = irq_spin_take(&mut->lock);
    task_t *self = THISCPU_GET(g_tid_prev);
    if (self != mut->owner) {
        panic("release mutex from %p, owner=%p\n", self, mut->owner);
    }

    // 尝试唤醒一个阻塞线程
    mut->owner = task_unpend_one(&mut->wq);
    irq_spin_give(&mut->lock, key);
    arch_task_switch();
}
