#include "sema.h"
#include <debug.h>


void sema_init(sema_t *sema, int initial, int limit) {
    sema->lock = SPIN_INIT;
    prioq_init(&sema->wq);
    sema->value = initial;
    sema->limit = limit;
}


static void sema_timeout(wdog_t *tmr) {
    waiter_t *waiter = containerof(tmr, waiter_t, timer);
    // sema_t *sema = containerof(waiter->wq, sema_t, wq);
    sema_t *sema = (sema_t*)waiter->user;
    raw_spin_take(&sema->lock);
    task_wake_timeout(&sema->wq, waiter);
    raw_spin_give(&sema->lock);
}

// 可能阻塞，不能在中断里调用
int sema_take(sema_t *sema, int timeout) {
    ASSERT(0 == cpu_int_depth());

    // 锁住 sema，持有 sema->lock 自旋锁
    int key = irq_spin_take(&sema->lock);

    if (sema->value > 0) {
        sema->value--;
        irq_spin_give(&sema->lock, key);
        return 1; // 获取成功
    }

    if (NOWAIT == timeout) {
        irq_spin_give(&sema->lock, key);
        return 0; // 获取失败，立即返回
    }

    // 没有取得信号量，需要阻塞
    waiter_t pender;
    pender.user = sema;
    task_pend(TS_PENDING, &sema->wq, &pender, timeout, sema_timeout);
    irq_spin_give(&sema->lock, key);
    arch_task_switch();

    // 恢复运行，检查是否因为超时而唤醒
    return !task_onresume(&pender);
}

// 不会阻塞，可以在中断里调用
void sema_give(sema_t *sema) {
    int key = irq_spin_take(&sema->lock);

    // 尝试唤醒一个阻塞的线程
    if (NULL == task_unpend_one(&sema->wq)) {
        // 没有阻塞者，增加计数器
        sema->value++;
        if (sema->value > sema->limit) {
            sema->value = sema->limit;
        }
    }
    irq_spin_give(&sema->lock, key);
    arch_task_switch();
}
