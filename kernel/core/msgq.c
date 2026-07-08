#include "msgq.h"
#include <page.h>
#include <debug.h>

void msgq_init(msgq_t *q) {
    void *va = vmspace_alloc(&g_kernel_vm, &q->rng,
        PAGE_SIZE, PT_MSGQ, MMU_WRITE);
    if (NULL == va) {
        panic("cannot create msgq\n");
    }
    q->rng.desc = "msgq";
    q->lock = SPINLOCK_INIT;
    prioq_init(&q->readers);
    prioq_init(&q->writers);
    fifo_init(&q->fifo, va, PAGE_SIZE);
}

size_t msgq_send(msgq_t *q, const void *msg, size_t len, int timeout) {
    ASSERT(cpu_int_depth() == 0);

    // task_t *self = current_task();
    spinlock_node_t node;

    while (1) {
        SPINLOCK_TAKE(&q->lock, &node);
        size_t wrote = fifo_write(&q->fifo, msg, len, len);
        if (wrote) {
            // 锁内 claim 一个阻塞的读者，锁外再唤醒
            task_t *reader = task_unpend_one_nolock(&q->readers);
            spinlock_give(&node);
            if (reader) {
                task_unpend_finish(reader);
                arch_task_switch();
            }
            return wrote;
        }

        // 没有写入数据
        if (NOWAIT == timeout) {
            spinlock_give(&node);
            return 0;
        }

        // 需要阻塞（调用方持锁）
        task_pend(&q->writers, &q->lock, timeout);
        timeout = NOWAIT; // 下次失败不再注册 wdog，直接返回
        spinlock_give(&node);
        arch_task_switch();

        // 被唤醒，但是数据还是在 fifo 里面，并没有读取出来，需要重试
    }
}

void msgq_send_force(msgq_t *q, void *msg, size_t len) {
    task_t *reader;
    {
        SPINLOCK_SCOPED(&q->lock);
        fifo_force_write(&q->fifo, msg, len);
        reader = task_unpend_one_nolock(&q->readers);
    }
    if (reader) {
        task_unpend_finish(reader);
        arch_task_switch();
    }
}

size_t msgq_recv(msgq_t *q, void *dst, size_t len, int timeout) {
    ASSERT(cpu_int_depth() == 0);

    // task_t *self = current_task();
    spinlock_node_t node;

    while (1) {
        SPINLOCK_TAKE(&q->lock, &node);
        size_t got = fifo_read(&q->fifo, dst, len, len);
        if (got) {
            // 锁内 claim 一个阻塞的写者，锁外再唤醒
            task_t *writer = task_unpend_one_nolock(&q->writers);
            spinlock_give(&node);
            if (writer) {
                task_unpend_finish(writer);
                arch_task_switch();
            }
            return got;
        }

        // 未读取数据
        if (NOWAIT == timeout) {
            spinlock_give(&node);
            return 0;
        }

        // 需要阻塞等待（调用方持锁）
        task_pend(&q->readers, &q->lock, timeout);
        timeout = NOWAIT; // 下次阻塞不再注册 wdog
        spinlock_give(&node);
        arch_task_switch();

        // 被唤醒，但数据还在 fifo 里面，没有读取出来
        // 需要重新锁住 msgq，尝试读取，如果读取失败还要继续阻塞
    }
}
