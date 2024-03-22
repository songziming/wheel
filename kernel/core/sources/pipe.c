// 消息队列，阻塞式单向数据通道

#include <wheel.h>
#include <fifo.h>



// 实现原理类似 semaphore，计数器用来表示当前 buffer 长度
// pipe 仅支持 FIFO 一种模式，不支持按优先级

// 信号量加上数据缓冲区，就是管道
// 没有关联数据读写的管道，就是信号量



typedef struct pipe {
    spin_t   spin;
    fifo_t   fifo;
    dlnode_t writers; // 排队等待写入数据的
    dlnode_t readers; // 排队等待读取数据的
} pipe_t;


typedef struct pender {
    dlnode_t dl;
    task_t  *tid;
    int      min;    // 最少读/写多少字节
    int      max;    // 最多读/写多少字节
    void    *buff;   // 从哪里写/读到哪里
    int      actual; // 实际读写多少字节
} pender_t;



// 为了方便，我们要求缓冲区大小必须是 2 的幂
void pipe_init(pipe_t *pipe, void *buff, size_t size) {
    ASSERT(NULL != pipe);

    // TODO 如果未指定 buff，则动态申请，并且 size 向上取整到 2 的幂

    pipe->spin = SPIN_INIT;
    fifo_init(&pipe->fifo, buff, size);
    dl_init_circular(&pipe->writers);
    dl_init_circular(&pipe->readers);
}


//------------------------------------------------------------------------------
// 写入数据（up）
//------------------------------------------------------------------------------

// 尝试向管道写入数据，至少写入 min，至多写入 max，返回实际写入的字节数
// 如果 min 字节也无法写入，则阻塞当前任务，至多阻塞 timeout，如果直到超时也没有成功写入 min，则返回 0
// 如果输入 timeout<=0，则不阻塞，如果无法写入 min，立即返回 0
// 如果超时时间为 FOREVER，表示一直阻塞，直到成功写入数据


// 写入数据失败
static void writer_wakeup(void *a1, void *a2) {
    ASSERT(cpu_int_depth());

    pipe_t *pipe = (pipe_t *)a1;
    pender_t *pender = (pender_t *)a2;

    int key = irq_spin_take(&pipe->spin);

    task_t *tid = pender->tid;

    // 首先要确认一下，任务可能被另一个 CPU 恢复
    dl_remove(&pender->dl);
    int cpu = sched_cont(tid, TASK_PENDING);

    irq_spin_give(&pipe->spin, key);

    if ((-1 != cpu) && (cpu_index() != cpu)) {
        arch_send_resched(cpu);
    }
}


size_t pipe_write(pipe_t *pipe, const void *src, size_t min, size_t max, int timeout) {
    ASSERT(0 == cpu_int_depth());
    ASSERT(NULL != pipe);
    ASSERT(min <= max);

    // 锁住管道对象，同时关闭中断，避免当前函数的执行被打断
    int key = irq_spin_take(&pipe->spin);

    // 如果成功写入数据，则可以直接返回
    size_t written = fifo_write(&pipe->fifo, src, min, max);
    if (written) {
        irq_spin_give(&pipe->spin, key);
        return written;
    }

    // 阻塞当前任务
    task_t *self = sched_stop_self(TASK_PENDING);

    pender_t pender;
    pender.tid = self;
    pender.min = min;
    pender.max = max;
    pender.buff = (void *)src;
    pender.actual = 0;
    dl_insert_before(&pender.dl, &pipe->writers);

    timer_t wakeup;
    if (timeout) {
        timer_start(&wakeup, timeout, writer_wakeup, pipe, &pender);
    }

    // 真的开始阻塞
    irq_spin_give(&pipe->spin, key);
    arch_task_switch();


    // 恢复运行，删除定时器
    if (timeout) {
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
    irq_spin_give(&pipe->spin, key);
}



//------------------------------------------------------------------------------
// 读取，不能在中断里调用
//------------------------------------------------------------------------------

// 该函数可以和 writer_wakeup 合并
static void reader_wakeup(void *a1, void *a2) {
    pipe_t *pipe = (pipe_t *)a1;
    pender_t *pender = (pender_t *)a2;

    int key = irq_spin_take(&pipe->spin);
    task_t *tid = pender->tid;

    // 首先要确认一下，任务可能被另一个 CPU 恢复
    dl_remove(&pender->dl);
    int cpu = sched_cont(tid, TASK_PENDING);

    irq_spin_give(&pipe->spin, key);
    if ((-1 != cpu) && (cpu_index() != cpu)) {
        arch_send_resched(cpu);
    }
}

size_t pipe_read(pipe_t *pipe, void *dst, size_t min, size_t max, int timeout) {
    ASSERT(0 == cpu_int_depth());
    ASSERT(NULL != pipe);
    ASSERT(NULL != dst);
    ASSERT(min <= max);

    int key = irq_spin_take(&pipe->spin);

    // 如果成功读取数据，则直接返回
    size_t got = fifo_read(&pipe->fifo, dst, min, max);
    if (got) {
        irq_spin_give(&pipe->spin, key);
        return got;
    }

    // 无法读取数据，准备阻塞等待
    task_t *self = sched_stop_self(TASK_PENDING);

    // 记录在阻塞队列中
    pender_t pender;
    pender.tid = self;
    pender.buff = dst;
    pender.min = min;
    pender.max = max;
    pender.actual = 0;
    dl_insert_before(&pender.dl, &pipe->readers);

    timer_t wakeup;
    if (timeout) {
        timer_start(&wakeup, timeout, reader_wakeup, pipe, &pender);
    }

    // 真的开始阻塞
    irq_spin_give(&pipe->spin, key);
    arch_task_switch();


    // 恢复运行，删除定时器
    if (timeout) {
        timer_cancel_sync(&wakeup);
    }

    return pender.actual;
}
