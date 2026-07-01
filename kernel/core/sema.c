#include "sema.h"
#include <debug.h>


void sema_init(sema_t *sema, int initial, int limit) {
    sema->lock = SPINLOCK_INIT;
    prioq_init(&sema->wq);
    sema->value = initial;
    sema->limit = limit;
}

// 可能阻塞，不能在中断里调用
int sema_take(sema_t *sema, int timeout) {
    ASSERT(0 == cpu_int_depth());

    {
        SPINLOCK_SCOPED(&sema->lock);
        if (sema->value > 0) {
            sema->value--;
            return 1; // 获取成功
        }
        if (NOWAIT == timeout) {
            return 0; // 获取失败，立即返回
        }
        // 没有取得信号量，阻塞当前任务
        task_pend(&sema->wq, &sema->lock, timeout);
    }

    arch_task_switch();
    return current_task()->got;
}

// 不会阻塞，可以在中断里调用
void sema_give(sema_t *sema) {
    task_t *tid;
    {
        // 锁内原子地判断：有阻塞者则 claim，否则 value++
        SPINLOCK_SCOPED(&sema->lock);
        tid = task_unpend_one_nolock(&sema->wq);
        if (NULL == tid) {
            sema->value++;
            if (sema->value > sema->limit) {
                sema->value = sema->limit;
            }
        }
    }
    // 锁外唤醒：避免 wdog_cancel 与超时回调死锁
    if (tid) {
        task_unpend_finish(tid);
    }
    arch_task_switch();
}
