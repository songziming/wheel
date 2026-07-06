#include "mutex.h"
#include <debug.h>

// 二值的信号量，但是更严格，必须由相同的线程获取/释放
// 不能跨线程获取/释放，也不能在 ISR 里面使用

void mutex_init(mutex_t *mut) {
    mut->lock = SPINLOCK_INIT;
    prioq_init(&mut->wq);
    mut->owner = NULL;
}

// 返回 1 表示成功得到锁
// 返回 0 表示未得到锁，超时
// TODO 返回 -1 表示锁被删除
int mutex_take(mutex_t *mut, int timeout) {
    ASSERT(0 == cpu_int_depth());

    task_t *self = current_task();

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
        // 没有得到，阻塞当前任务
        task_pend(&mut->wq, &mut->lock, timeout);
    }

    arch_task_switch();
    return self->got;
}

void mutex_give(mutex_t *mut) {
    ASSERT(0 == cpu_int_depth());
    task_t *tid;
    {
        // 锁内原子地 claim 下一个等待者并交接 owner
        SPINLOCK_SCOPED(&mut->lock);
        task_t *self = current_task();
        if (self != mut->owner) {
            panic("release mutex from %p, owner=%p\n", self, mut->owner);
        }
        tid = task_unpend_one_nolock(&mut->wq);
        mut->owner = tid; // NULL 表示无人等待
    }
    // 锁外唤醒
    if (tid) {
        task_unpend_finish(tid);
        arch_task_switch();
    }
}

// TODO mutex_destroy 删除一个互斥锁
//  按照 posix，只有当 mutex_destroy 没有阻塞者的时候才能释放，否则返回 EBUSY
//  我们可以支持两种模式，safe_destroy 和 force_destroy
