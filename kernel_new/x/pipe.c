// 消息队列，阻塞式单向数据通道

#include "pipe.h"
// #include <wheel.h>
#include <library/debug.h>
#include <proc/sched.h>



// 实现原理类似 semaphore，计数器用来表示当前 buffer 长度
// pipe 仅支持 FIFO 一种模式，不支持按优先级

// 信号量加上数据缓冲区，就是管道
// 没有关联数据读写的管道，就是信号量


typedef struct pender {
    dlnode_t dl;
    task_t  *tid;
    int      min;    // 最少读/写多少字节
    int      max;    // 最多读/写多少字节
    void    *buff;   // 从哪里写/读到哪里
    int      actual; // 实际读写多少字节
} pender_t;


#define NOWAIT -1
#define FOREVER -2


//------------------------------------------------------------------------------
// 初始化
//------------------------------------------------------------------------------

void pipe_init(pipe_t *pipe, void *buff, size_t size) {
    ASSERT(NULL != pipe);

    // TODO 如果未指定 buff，则动态申请，并且 size 向上取整到 2 的幂

    spin_init(&pipe->spin); // pipe->spin = SPIN_INIT;
    fifo_init(&pipe->fifo, buff, size);
    dl_init_circular(&pipe->writers);
    dl_init_circular(&pipe->readers);
}



//------------------------------------------------------------------------------
// 唤醒队列中阻塞的任务，已经持有锁，返回需要，返回需要调度的 cpu 位图
//------------------------------------------------------------------------------

static cpuset_t unpend_writers(pipe_t *pipe) {
    cpuset_t resched_mask = 0;

    dlnode_t *node = pipe->writers.next;
    while ((node != &pipe->writers) && (fifo_left_size(&pipe->fifo) > 0)) {
        pender_t *pender = containerof(node, pender_t, dl);
        node = node->next;

        size_t len = fifo_write(&pipe->fifo, pender->buff, pender->min, pender->max);
        if (0 == len) {
            continue;
        }

        pender->actual = len;
        dl_remove(&pender->dl);

        int cpu = sched_cont(pender->tid, TASK_PENDING);
        if (cpu >= 0) {
            resched_mask |= 1UL << cpu;
        }
    }

    return resched_mask;
}

static cpuset_t unpend_readers(pipe_t *pipe) {
    cpuset_t resched_mask = 0;

    dlnode_t *node = pipe->readers.next;
    while (node != &pipe->readers) {
        pender_t *pender = containerof(node, pender_t, dl);
        node = node->next;

        size_t len = fifo_read(&pipe->fifo, pender->buff, pender->min, pender->max);
        if (0 == len) {
            continue;
        }

        pender->actual = len;
        dl_remove(&pender->dl);

        int cpu = sched_cont(pender->tid, TASK_PENDING);
        if (cpu >= 0) {
            resched_mask |= 1UL << cpu;
        }
    }

    return resched_mask;
}



//------------------------------------------------------------------------------
// 唤醒超时的阻塞任务，在中断里运行
//------------------------------------------------------------------------------

static void wakeup_writer(void *a1, void *a2) {
    ASSERT(cpu_int_depth());

    pipe_t *pipe = (pipe_t *)a1;
    pender_t *pender = (pender_t *)a2;

    int key = irq_spin_take(&pipe->spin);
    task_t *tid = pender->tid;

    // 首先要确认一下，任务可能已经被另一个 CPU 恢复
    if (!dl_contains(&pipe->writers, &pender->dl)) {
        irq_spin_give(&pipe->spin, key);
        return;
    }

    dl_remove(&pender->dl);
    int cpu = sched_cont(tid, TASK_PENDING);
    irq_spin_give(&pipe->spin, key);

    if ((-1 != cpu) && (cpu_index() != cpu)) {
        arch_ipi_resched(cpu);
    }
}

static void wakeup_reader(void *a1, void *a2) {
    ASSERT(cpu_int_depth());

    pipe_t *pipe = (pipe_t *)a1;
    pender_t *pender = (pender_t *)a2;

    int key = irq_spin_take(&pipe->spin);
    task_t *tid = pender->tid;

    // 首先要确认一下，任务可能已经被另一个 CPU 恢复
    if (!dl_contains(&pipe->readers, &pender->dl)) {
        irq_spin_give(&pipe->spin, key);
        return;
    }

    // 首先要确认一下，任务可能被另一个 CPU 恢复
    dl_remove(&pender->dl);
    int cpu = sched_cont(tid, TASK_PENDING);
    irq_spin_give(&pipe->spin, key);

    if ((-1 != cpu) && (cpu_index() != cpu)) {
        arch_ipi_resched(cpu);
    }
}



//------------------------------------------------------------------------------
// 写入数据（up）
//------------------------------------------------------------------------------

size_t pipe_write(pipe_t *pipe, const void *src, size_t min, size_t max, int timeout) {
    ASSERT(0 == cpu_int_depth());
    ASSERT(NULL != pipe);
    ASSERT(min <= max);

    // 锁住管道对象，同时关闭中断，避免当前函数的执行被打断
    int key = irq_spin_take(&pipe->spin);

    // 如果成功写入数据，则唤醒正在阻塞的 reader，然后返回
    size_t written = fifo_write(&pipe->fifo, src, min, max);
    if (written) {
        cpuset_t cpus = unpend_readers(pipe);
        irq_spin_give(&pipe->spin, key);
        notify_resched(cpus);
        return written;
    }

    // 没有写入数据，如果不等待，则立即返回
    if (NOWAIT == timeout) {
        irq_spin_give(&pipe->spin, key);
        return 0;
    }

    // 阻塞当前任务
    task_t *self = sched_stop(TASK_PENDING);

    // 放在阻塞队列中
    pender_t pender;
    pender.tid = self;
    pender.min = min;
    pender.max = max;
    pender.buff = (void *)src;
    pender.actual = 0;
    dl_insert_before(&pender.dl, &pipe->writers);

    // 创建超时提醒
    timer_t wakeup;
    if (FOREVER != timeout) {
        timer_start(&wakeup, timeout, wakeup_writer, pipe, &pender);
    }

    // 真的开始阻塞
    irq_spin_give(&pipe->spin, key);
    arch_task_switch();

    // 恢复运行，删除定时器
    if (FOREVER != timeout) {
        timer_cancel_sync(&wakeup);
    }

    return pender.actual;
}

// 如果写入的数据超过了容量限制，则覆盖最早的数据
// 无阻塞，可以在中断里使用
void pipe_force_write(pipe_t *pipe, const void *src, size_t len) {
    ASSERT(NULL != pipe);

    int key = irq_spin_take(&pipe->spin);
    fifo_force_write(&pipe->fifo, src, len);
    cpuset_t cpus = unpend_readers(pipe);
    irq_spin_give(&pipe->spin, key);

    notify_resched(cpus);
}



//------------------------------------------------------------------------------
// 读取数据（down），不能在中断里调用
//------------------------------------------------------------------------------

size_t pipe_read(pipe_t *pipe, void *dst, size_t min, size_t max, int timeout) {
    ASSERT(0 == cpu_int_depth());
    ASSERT(NULL != pipe);
    ASSERT(NULL != dst);
    ASSERT(min <= max);

    int key = irq_spin_take(&pipe->spin);

    // 如果成功读取数据，则直接返回
    size_t got = fifo_read(&pipe->fifo, dst, min, max);
    if (got) {
        cpuset_t cpus = unpend_writers(pipe);
        irq_spin_give(&pipe->spin, key);
        notify_resched(cpus);
        return got;
    }

    // 无法写入数据，立即返回
    if (NOWAIT == timeout) {
        irq_spin_give(&pipe->spin, key);
        return 0;
    }

    // 阻塞当前任务
    task_t *self = sched_stop(TASK_PENDING);

    // 放在阻塞队列中
    pender_t pender;
    pender.tid = self;
    pender.buff = dst;
    pender.min = min;
    pender.max = max;
    pender.actual = 0;
    dl_insert_before(&pender.dl, &pipe->readers);

    // 创建超时提醒
    timer_t wakeup;
    if (FOREVER != timeout) {
        timer_start(&wakeup, timeout, wakeup_reader, pipe, &pender);
    }

    // 真的开始阻塞
    irq_spin_give(&pipe->spin, key);
    arch_task_switch();

    // 恢复运行，删除定时器
    if (FOREVER != timeout) {
        timer_cancel_sync(&wakeup);
    }

    return pender.actual;
}
