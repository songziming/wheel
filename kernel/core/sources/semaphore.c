// 信号量，一个共享整型变量，表示可用资源数量，支持两种操作：
// - take，减小取值，若当前取值不足，则当前任务阻塞
// - give，增加取值，唤醒被阻塞的任务

#include <semaphore.h>
#include <wheel.h>


// 代表一个阻塞的任务
typedef struct pend_item {
    dlnode_t  dl;
    task_t   *task;
    int       require;
    int       got;
} pend_item_t;



//------------------------------------------------------------------------------
// 初始化信号量
//------------------------------------------------------------------------------

static void semaphore_init(semaphore_t *sem, int initial, int max) {
    ASSERT(NULL != sem);
    ASSERT(initial >= 0);
    ASSERT(max > 0);

    if (initial > max) {
        initial = max;
    }

    sem->spin = SPIN_INIT;
    sem->limit = max;
    sem->value = initial;
}

void fifo_semaphore_init(fifo_semaphore_t *sem, int initial, int max) {
    semaphore_init(&sem->common, initial, max);
    dl_init_circular(&sem->penders);
}

void priority_semaphore_init(priority_semaphore_t *sem, int initial, int max) {
    semaphore_init(&sem->common, initial, max);
    priority_q_init(&sem->penders);
}



//------------------------------------------------------------------------------
// 超时处理函数，在中断里执行
//------------------------------------------------------------------------------

// 尚未得到信号量，但等待时间已过
static void semaphore_wakeup(semaphore_t *sem, pend_item_t *item,
        dlnode_t *fifo_q, priority_q_t *priority_q) {
    ASSERT(cpu_int_depth());
    ASSERT(NULL != sem);
    ASSERT(NULL != item);

    task_t *task = item->task;

    int key = irq_spin_take(&sem->spin);
    if (fifo_q) {
        ASSERT(dl_contains(fifo_q, &item->dl));
        dl_remove(&item->dl);
    }
    if (priority_q) {
        priority_q_remove(priority_q, task, &item->dl);
    }
    int cpu = sched_cont(task, TASK_PENDING); // 恢复任务
    irq_spin_give(&sem->spin, key);

    // 如果任务在当前 CPU 恢复，则无需切换任务，因为目前就处在中断
    if ((-1 != cpu) && (cpu_index() != cpu)) {
        arch_send_resched(cpu);
    }
}

static void fifo_semaphore_wakeup(void *arg1, void *arg2) {
    fifo_semaphore_t *sem = (fifo_semaphore_t *)arg1;
    pend_item_t *item = (pend_item_t *)arg2;
    semaphore_wakeup(&sem->common, item, &sem->penders, NULL);
}

static void priority_semaphore_wakeup(void *arg1, void *arg2) {
    priority_semaphore_t *sem = (priority_semaphore_t *)arg1;
    pend_item_t *item = (pend_item_t *)arg2;
    semaphore_wakeup(&sem->common, item, NULL, &sem->penders);
}



//------------------------------------------------------------------------------
// 获取信号量，可能阻塞，不能在中断内调用
//------------------------------------------------------------------------------

// 如果成功获得信号量，则返回非零
// 如果获取信号量失败（例如超时、信号量被删除），则返回零

static int semaphore_take(semaphore_t *sem, int n, int timeout,
        dlnode_t *fifo_q, priority_q_t *priority_q, timer_func_t wakup_func) {
    ASSERT(0 == cpu_int_depth());
    ASSERT(NULL != sem);
    ASSERT(n > 0);

    // 锁住中断，避免当前任务的执行被打断
    int key = irq_spin_take(&sem->spin);

    // 如果资源足够，可以直接返回，不用阻塞
    if (sem->value >= n) {
        sem->value -= n;
        irq_spin_give(&sem->spin, key);
        return n;
    }

    // 当前任务变为阻塞态，但开启中断前还能保持运行
    task_t *self = sched_stop_self(TASK_PENDING);

    // 创建一个 pender，添加到阻塞队列
    pend_item_t item;
    item.task = self;
    item.require = n;
    item.got = 0;
    if (fifo_q) {
        dl_insert_before(&item.dl, fifo_q);
    }
    if (priority_q) {
        priority_q_push(priority_q, self, &item.dl);
    }

    // 指定了超时时间，则开启一个 timer
    timer_t wakeup;
    if (wakup_func && (timeout > 0)) {
        timer_start(&wakeup, timeout, wakup_func, &sem, &item);
    }

    // 放开锁，切换到其他任务
    irq_spin_give(&sem->spin, key);
    arch_task_switch();

    // 重新恢复运行，把计时器停止
    if (wakup_func && (timeout > 0)) {
        timer_cancel(&wakeup);
    }

    // 检查原因（因为得到了信号量，还是因为超时）
    return item.got;
}

int fifo_semaphore_take(fifo_semaphore_t *sem, int n, int timeout) {
    return semaphore_take(&sem->common, n, timeout, &sem->penders, NULL, fifo_semaphore_wakeup);
}

int priority_semaphore_take(priority_semaphore_t *sem, int n, int timeout) {
    return semaphore_take(&sem->common, n, timeout, NULL, &sem->penders, priority_semaphore_wakeup);
}



//------------------------------------------------------------------------------
// 释放信号量，将阻塞的任务恢复，可以在任务和中断里执行
//------------------------------------------------------------------------------

static void multi_send_resched(uint64_t mask) {
    int sched_self = mask & (1UL << cpu_index());
    mask &= ~(1UL << cpu_index());

    for (int i = 0; i < cpu_count(); ++i, mask >>= 1) {
        if (mask & 1) {
            arch_send_resched(i);
        }
    }

    if (sched_self) {
        arch_task_switch();
    }
}

void fifo_semaphore_give(fifo_semaphore_t *sem, int n) {
    ASSERT(NULL != sem);
    ASSERT(n > 0);

    semaphore_t *common = &sem->common;
    dlnode_t *pend_q = &sem->penders;

    int key = irq_spin_take(&common->spin);

    common->value += n;
    if (common->value > common->limit) {
        common->value = common->limit;
    }

    uint64_t resched_mask = 0;
    while ((common->value > 0) && !dl_is_lastone(pend_q)) {
        pend_item_t *item = containerof(pend_q->next, pend_item_t, dl);
        if (common->value < item->require) {
            continue;
        }

        dl_remove(&item->dl);
        common->value -= item->require;
        item->got = item->require;

        task_t *task = item->task;
        int cpu = sched_cont(task, TASK_PENDING); // 恢复任务
        resched_mask |= 1UL << cpu;
    }

    irq_spin_give(&common->spin, key);
    multi_send_resched(resched_mask);
}

void priority_semaphore_give(priority_semaphore_t *sem, int n) {
    ASSERT(NULL != sem);
    ASSERT(n > 0);

    semaphore_t *common = &sem->common;
    priority_q_t *pend_q = &sem->penders;

    int key = irq_spin_take(&common->spin);

    common->value += n;
    if (common->value > common->limit) {
        common->value = common->limit;
    }

    uint64_t resched_mask = 0;
    while (common->value > 0) {
        dlnode_t *head = priority_q_head(pend_q);
        if (NULL == head) {
            break;
        }

        pend_item_t *item = containerof(head, pend_item_t, dl);
        if (common->value < item->require) {
            continue;
        }

        task_t *task = item->task;
        priority_q_remove(pend_q, task, &item->dl);
        common->value -= item->require;
        item->got = item->require;

        int cpu = sched_cont(task, TASK_PENDING); // 恢复任务
        resched_mask |= 1UL << cpu;
    }

    irq_spin_give(&common->spin, key);
    multi_send_resched(resched_mask);
}
