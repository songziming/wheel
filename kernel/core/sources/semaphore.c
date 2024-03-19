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


// TODO
// 目前的做法，实在任务栈上创建一个 pend_item，记录阻塞者
// 如果 OS 尝试删除一个正在阻塞的任务，是无法找到这个 pend_item 的
// 应该使用 task_t 里面的 q_node，并通过指针记录这个 task 位于哪个阻塞队列中


// TODO 需要准备一套直接操作任务状态的函数
//      除了更新任务状态、就绪队列，还检查能否抢占，发送 IPI



// 尚未得到信号量，但等待时间已过
static void semaphore_wakeup(void *arg1, void *arg2) {
    pend_item_t *pender = (pend_item_t *)arg1;
    semaphore_t *sema = (semaphore_t *)arg2;

    task_t *task = pender->task;

    int key = irq_spin_take(&sema->spin);
    raw_spin_take(&task->spin);

    dl_remove(&pender->dl);
    int cpu = sched_cont(task, TASK_PENDING); // 恢复任务

    raw_spin_give(&task->spin);
    irq_spin_give(&sema->spin, key);

    if (cpu_index() == cpu) {
        arch_task_switch();
    } else if (-1 != cpu) {
        arch_send_resched(cpu);
    }
}




// 可能阻塞
void semaphore_take(semaphore_t *sem, int n, int timeout) {
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

    // 指定了超时时间，则开启一个 timer
    timer_t wakeup;
    if (timeout) {
        timer_start(&wakeup, timeout, semaphore_wakeup, &item, &sem);
    }

    // 阻塞当前任务
    raw_spin_take(&self->spin);
    sched_stop(self, TASK_PENDING);
    raw_spin_give(&self->spin);
    irq_spin_give(&sem->spin, key);

    // TODO 有一种可能，将自身任务放入了阻塞队列，即将切换任务，立马得到了信号量
    //      如果这时切换任务，会怎样？

    arch_task_switch();

    // TODO 任务重新开始执行，判断是否成功获得信号量
    //      还是因为超时或删除信号量导致的重启
    ASSERT(!dl_contains(&sem->pend_q, &item.dl));
}



// extern task_t g_shell_tcb;


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
        int cpu = sched_cont(task, TASK_PENDING); // 恢复任务
        // int cpu = task->last_cpu;
        // uint16_t state = task->state;
        raw_spin_give(&task->spin);

        if (cpu_index() == cpu) {
            resched_this_cpu = 1; // 现在先不切换，还要恢复后面的阻塞任务
        } else if (-1 != cpu) {
            // TODO 也可以设置一个 位图flag，遍历完毕后发送多播
            arch_send_resched(cpu);
        }
    }

    irq_spin_give(&sem->spin, key);

    if (resched_this_cpu) {
        arch_task_switch();
    }
}
