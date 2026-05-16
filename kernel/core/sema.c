#include "sema.h"
#include <debug.h>


void sema_init(sema_t *sema, int initial, int limit) {
    // sema->wq
    prioq_init(&sema->wq);
    sema->value = initial;
    sema->limit = limit;
}

// 可能阻塞，不能在中断里调用
int sema_take(sema_t *sema, int timeout) {
    ASSERT(0 == cpu_int_depth());

    // 锁住 sema，持有 sema->lock 自旋锁
    int key = irq_spin_take(&sema->wq.lock);

    if (sema->value > 0) {
        sema->value--;
        irq_spin_give(&sema->wq.lock, key);
        return 1; // 获取成功
    }

    if (NOWAIT == timeout) {
        irq_spin_give(&sema->wq.lock, key);
        return 0; // 获取失败，立即返回
    }

    // 调用 task_stop 自动释放了 wq 自旋锁
    if (task_stop(TS_PENDING, &sema->wq, key, timeout)) {
        // 因为超时而返回
        return 0;
    }

    // 因为其他任务 give，得到了信号量
    return 1;
}

// 不会阻塞，可以在中断里调用
void sema_give(sema_t *sema) {
    int key = irq_spin_take(&sema->wq.lock);

    // 这段逻辑比较通用，可以放在 task.c 里面
    dlnode_t *dl = prioq_head_nolock(&sema->wq);
    if (dl) {
        // 存在阻塞者，将其唤醒，无需操作计数器
        waiter_t *w = containerof(dl, waiter_t, dl);
        prioq_remove_nolock(&sema->wq, dl, w->tid->priority);
        int cpu = task_cont(w->tid, TS_PENDING);
        irq_spin_give(&sema->wq.lock, key);

        if (cpu >= 0) {
            if (cpu_index() != cpu) {
                arch_send_ipi(cpu, VEC_IPI_RESCHED);
            } else {
                arch_task_switch();
            }
        }
        return;
    }

    // 没有阻塞者，增加计数器
    sema->value++;
    if (sema->value > sema->limit) {
        sema->value = sema->limit;
    }
    irq_spin_give(&sema->wq.lock, key);
}
