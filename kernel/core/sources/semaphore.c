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
} pend_item_t;


void semaphore_init(semaphore_t *sem, int n, int max) {
    ASSERT(NULL != sem);
    ASSERT(n >= 0);
    ASSERT(max > 0);

    if (n > max) {
        n = max;
    }

    sem->spin = SPIN_INIT;
    dl_init_circular(&sem->pend_q);
    sem->limit = max;
    sem->value = n;
}

// 可能阻塞
void semaphore_take(semaphore_t *sem, int n) {
    ASSERT(NULL != sem);
    ASSERT(n > 0);

    int key = irq_spin_take(&sem->spin);
    if (sem->value >= n) {
        sem->value -= n;
        irq_spin_give(&sem->spin, key);
        return;
    }

    // 在栈上创建一个 pend 节点，添加到信号量的阻塞队列中
    // 阻塞任务的栈空间依然有效，正好可以用起来
    pend_item_t item;
    task_t *self = THISCPU_GET(g_tid_prev);
    item.task = self;
    item.require = n;
    dl_insert_before(&item.dl, &sem->pend_q);

    // TODO 可以指定一个超时时间，使用 watchdog 唤醒任务，并返回失败

    // 阻塞当前任务
    raw_spin_take(&self->spin);
    uint16_t old = sched_stop(self, TASK_PENDING);
    ASSERT(TASK_READY == old); // 当前任务必然处于就绪态
    raw_spin_give(&self->spin);
    irq_spin_give(&sem->spin, key);

    // TODO 有一种可能，将自身任务放入了阻塞队列，即将切换任务，立马得到了信号量
    //      如果这时切换任务，会怎样？

    arch_task_switch();

    // TODO 任务重新开始执行，判断是否成功获得信号量
    //      还是因为超时或删除信号量导致的重启
    ASSERT(!dl_contains(&sem->pend_q, &item.dl));
}



extern task_t g_shell_tcb;


void semaphore_give(semaphore_t *sem, int n) {
    ASSERT(NULL != sem);
    ASSERT(n > 0);

    int key = irq_spin_take(&sem->spin);

    sem->value += n;
    if (sem->value > sem->limit) {
        sem->value = sem->limit;
    }

    // 如果没有阻塞的任务就直接返回
    if (dl_is_lastone(&sem->pend_q)) {
        irq_spin_give(&sem->spin, key);
// #if 1
//         key = irq_spin_take(&g_shell_tcb.spin);
//         klog("(%d)", g_shell_tcb.state);
//         if (g_shell_tcb.state) {
//             klog("(%p:%p)", g_shell_tcb.q_node.prev, g_shell_tcb.q_node.next);
//         }
//         irq_spin_give(&g_shell_tcb.spin, key);
// #endif
        return;
    }

    char resched_this_cpu = 0;

    // 遍历阻塞队列，尝试唤醒
    for (dlnode_t *i = sem->pend_q.next; i != &sem->pend_q;) {
        pend_item_t *item = containerof(i, pend_item_t, dl);
        i = i->next; // 遍历的同时要删除节点，需要提前得到下一个 node

        if (sem->value < item->require) {
            klog("(nowake)");
            continue;
        }

        sem->value -= item->require;
        dl_remove(&item->dl);

        task_t *task = item->task;
        raw_spin_take(&task->spin);
        sched_cont(task, TASK_PENDING); // 恢复任务
        int cpu = task->last_cpu;
        uint16_t state = task->state;
        raw_spin_give(&task->spin);

        if (TASK_READY != state) {
            continue; // 任务还在等待其他资源
        }

        if (cpu_index() != cpu) {
            arch_send_resched(cpu); // 给目标 CPU 发送 IPI，触发调度
        } else {
            resched_this_cpu = 1; // 现在先不切换，还要恢复后面的阻塞任务
        }
    }

    irq_spin_give(&sem->spin, key);

    if (resched_this_cpu) {
        arch_task_switch();
    }
}
