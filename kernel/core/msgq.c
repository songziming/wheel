#include "msgq.h"
#include <page.h>
#include <debug.h>

void msgq_init(msgq_t *q) {
    q->lock = SPIN_INIT;

    prioq_init(&q->readers);
    prioq_init(&q->writers);

    size_t pa = PAGE_ALLOC(0, PT_MSGQ);
    size_t va = vmspace_valloc(&g_kernel_vm, &q->rng, POOL_ZONE_START, POOL_ZONE_END, PAGE_SIZE * 2);
    mmu_map(g_kernel_vm.table, va, va+PAGE_SIZE, pa, MMU_WRITE);
    mmu_map(g_kernel_vm.table, va+PAGE_SIZE, va+PAGE_SIZE*2, pa, MMU_WRITE);
    q->rng.paddr = pa;

    fifo_init(&q->fifo, (void*)va, PAGE_SIZE);
}

static void writer_timeout(wdog_t *wd) {
    waiter_t *waiter = containerof(wd, waiter_t, timer);
    msgq_t *q = (msgq_t*)waiter->user;
    raw_spin_take(&q->lock);
    task_wake_timeout(&q->writers, waiter);
    raw_spin_give(&q->lock);
}

static void reader_timeout(wdog_t *wd) {
    waiter_t *waiter = containerof(wd, waiter_t, timer);
    msgq_t *q = (msgq_t*)waiter->user;
    raw_spin_take(&q->lock);
    task_wake_timeout(&q->readers, waiter);
    raw_spin_give(&q->lock);
}

size_t msgq_send(msgq_t *q, const void *msg, size_t len, int timeout) {
    ASSERT(cpu_int_depth() == 0);

    waiter_t pender;
    pender.user = q;
    pender.expired = 0;

    while (1) {
        int key = irq_spin_take(&q->lock);
        if (pender.expired) { // 持有锁才能检查超时
            irq_spin_give(&q->lock, key);
            return 0; // 超时则直接返回
        }

        size_t wrote = fifo_write(&q->fifo, msg, len, len);
        if (wrote) {
            task_onresume(&pender); // 删除 timeout wdog
            task_unpend_one(&q->readers);
            irq_spin_give(&q->lock, key);
            arch_task_switch();
            return wrote;
        }

        // 没有写入数据
        if (NOWAIT == timeout) {
            irq_spin_give(&q->lock, key);
            return 0;
        }

        // 需要阻塞
        task_pend(&q->writers, &pender, timeout, writer_timeout);
        timeout = FOREVER;
        irq_spin_give(&q->lock, key);
        arch_task_switch();

        // TODO 被唤醒，但是数据还是在 fifo 里面，并没有读取出来
        // 需要重试一遍
    }
}

void msgq_send_force(msgq_t *q, void *msg, size_t len) {
    int key = irq_spin_take(&q->lock);
    fifo_force_write(&q->fifo, msg, len);
    task_unpend_one(&q->readers);
    irq_spin_give(&q->lock, key);
    arch_task_switch();
}

size_t msgq_recv(msgq_t *q, void *dst, size_t len, int timeout) {
    ASSERT(cpu_int_depth() == 0);

    waiter_t pender;
    pender.user = q;
    pender.expired = 0;

    while (1) {
        int key = irq_spin_take(&q->lock);
        if (pender.expired) {
            irq_spin_give(&q->lock, key);
            return 0; // 超时则直接返回
        }

        size_t got = fifo_read(&q->fifo, dst, len, len);
        if (got) {
            task_onresume(&pender); // 将上次的 wdog 删除
            task_unpend_one(&q->writers);
            irq_spin_give(&q->lock, key);
            arch_task_switch();
            return got;
        }

        // 未读取数据
        if (NOWAIT == timeout) {
            irq_spin_give(&q->lock, key);
            return 0;
        }

        // 需要阻塞等待
        task_pend(&q->readers, &pender, timeout, reader_timeout);
        timeout = FOREVER; // 下次阻塞不再注册 wdog
        irq_spin_give(&q->lock, key);
        arch_task_switch();

        // 被唤醒，但数据还在 fifo 里面，没有读取出来
        // 需要重新锁住 msgq，尝试读取，如果读取失败还要继续阻塞
        // 保持 wdog 在队列里面，这样重新阻塞就无需重新设置超时了
    }
}
