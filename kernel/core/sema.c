#include "sema.h"
#include <debug.h>


void sema_init(sema_t *sema, int initial, int limit) {
    sema->lock = SPINLOCK_INIT;
    prioq_init(&sema->wq);
    sema->value = initial;
    sema->limit = limit;
}

static void sema_timeout(wdog_t *tmr) {
    waiter_t *waiter = containerof(tmr, waiter_t, timer);
    sema_t *sema = (sema_t*)waiter->user;
    {
        RAW_LOCK_SCOPED(&sema->lock);
        task_wake_timeout(&sema->wq, waiter);
    }
}

// 可能阻塞，不能在中断里调用
int sema_take(sema_t *sema, int timeout) {
    ASSERT(0 == cpu_int_depth());

    // 锁住 sema，持有 sema->lock 自旋锁
    waiter_t pender;
    {
        IRQ_LOCK_SCOPED(&sema->lock);
        if (sema->value > 0) {
            sema->value--;
            return 1; // 获取成功
        }
        if (NOWAIT == timeout) {
            return 0; // 获取失败，立即返回
        }

        // 没有取得信号量，需要阻塞
        pender.user = sema;
        task_pend(&sema->wq, &pender, timeout, sema_timeout);
    }
    arch_task_switch();

    // 恢复运行，检查是否因为超时而唤醒
    task_onresume(&pender);
    return pender.got;
}

// 不会阻塞，可以在中断里调用
void sema_give(sema_t *sema) {
    {
        IRQ_LOCK_SCOPED(&sema->lock);
        // 尝试唤醒一个阻塞的线程
        if (NULL == task_unpend_one(&sema->wq)) {
            // 没有阻塞者，增加计数器
            sema->value++;
            if (sema->value > sema->limit) {
                sema->value = sema->limit;
            }
        }
    }
    arch_task_switch();
}
