#include "mutex.h"
#include <debug.h>

// 二值的信号量，但是更严格，必须由相同的线程获取/释放
// 不能跨线程获取/释放，也不能在 ISR 里面使用

void mutex_init(mutex_t *mut) {
    mut->lock = SPINLOCK_INIT;
    prioq_init(&mut->wq);
    mut->owner = NULL;
}

static void mutex_timeout(wdog_t *tmr) {
    waiter_t *waiter = containerof(tmr, waiter_t, timer);
    mutex_t *mut = (mutex_t*)waiter->user;
    SPINLOCK_SCOPED(&mut->lock);
    task_wake_timeout(&mut->wq, waiter);
}

// 返回 1 表示成功得到锁
// 返回 0 表示未得到锁，超时
// TODO 返回 -1 表示锁被删除
int mutex_take(mutex_t *mut, int timeout) {
    ASSERT(0 == cpu_int_depth());

    task_t *self = current_task();
    waiter_t pender;

    {
        SPINLOCK_SCOPED(&mut->lock);
        ASSERT(self != mut->owner); // 不许重入
        if (NULL == mut->owner) {
            mut->owner = self;
            return 1;
        }
        if (NOWAIT == timeout) {
            return 0;
        }
        // 没有得到，需要阻塞
        pender.user = mut;
        task_pend(&mut->wq, &pender, timeout, mutex_timeout);
    }
    arch_task_switch();

    // 恢复运行，检查是否因为超时而唤醒
    task_onresume(&pender);
    return pender.got;
}

void mutex_give(mutex_t *mut) {
    ASSERT(0 == cpu_int_depth());
    {
        SPINLOCK_SCOPED(&mut->lock);
        task_t *self = current_task();
        if (self != mut->owner) {
            panic("release mutex from %p, owner=%p\n", self, mut->owner);
        }
        mut->owner = task_unpend_one(&mut->wq); // 唤醒一个阻塞线程
    }
    arch_task_switch();
}

// TODO mutex_destroy 删除一个互斥锁
//  按照 posix，只有当 mutex_destroy 没有阻塞者的时候才能释放，否则返回 EBUSY
//  我们可以支持两种模式，safe_destroy 和 force_destroy
