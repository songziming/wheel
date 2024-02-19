#include <sched.h>
#include <wheel.h>

#include <spin.h>
#include <shell.h>

// dllist 双链表没有专门的头节点，因此用指针

typedef struct ready_q {
    spin_t    spin;
    uint32_t  total_tick; // 队列中所有任务的时间片之和
    uint32_t  priorities; // 位图，表示队列包含的优先级
    dlnode_t *heads[PRIORITY_NUM];
} ready_q_t;


//------------------------------------------------------------------------------
// 就绪队列
//------------------------------------------------------------------------------

static INIT_TEXT void ready_q_init(ready_q_t *q) {
    ASSERT(NULL != q);

    q->spin = SPIN_INIT;
    q->total_tick = 0;
    q->priorities = 0;
    memset(q->heads, 0, PRIORITY_NUM * sizeof(dlnode_t *));
}

// 调用下面的函数，需要 caller 已经获取就绪队列的自旋锁

// 返回就绪队列中优先级最高的任务
static task_t *ready_q_head(ready_q_t *q) {
    ASSERT(NULL != q);
    ASSERT(q->spin.ticket_counter > q->spin.service_counter);

    int top = __builtin_ctz(q->priorities);
    ASSERT(NULL != q->heads[top]);
    return containerof(q->heads[top], task_t, q_node);
}

static void ready_q_insert(ready_q_t *q, task_t *task) {
    ASSERT(NULL != q);
    ASSERT(q->spin.ticket_counter > q->spin.service_counter);
    ASSERT(NULL != task);

    int pri = task->priority;

    if ((1U << pri) & q->priorities) {
        ASSERT(NULL != q->heads[pri]);
        dl_insert_before(&task->q_node, q->heads[pri]);
    } else {
        ASSERT(NULL == q->heads[pri]);
        dl_init_circular(&task->q_node);
        q->heads[pri] = &task->q_node;
        q->priorities |= 1U << pri;
    }

    q->total_tick += task->tick_reload;
}

static void ready_q_remove(ready_q_t *q, task_t *task) {
    ASSERT(NULL != q);
    ASSERT(q->spin.ticket_counter > q->spin.service_counter);
    ASSERT(NULL != task);
    ASSERT(q->total_tick >= task->tick_reload);

    int pri = task->priority;
    ASSERT(NULL != q->heads[pri]);
    ASSERT(q->priorities & (1U << pri));

    dlnode_t *next = dl_remove(&task->q_node);

    if (NULL == next) {
        q->heads[pri] = NULL;
        q->priorities &= ~(1U << pri);
        return;
    }
    if (q->heads[pri] == &task->q_node) {
        q->heads[pri] = next;
    }

    q->total_tick -= task->tick_reload;
}



//------------------------------------------------------------------------------
// 多核任务调度，任务执行状态控制
//------------------------------------------------------------------------------

static PCPU_BSS ready_q_t g_ready_q; // 就绪队列

// 在这里 g_tid_prev 是只读的，只有 ISR（entries.S）能更新这个变量
// 就绪队列自旋锁 g_ready_q->spin 同时也控制 g_tid_next（以及每个任务中的 q_node）
PCPU_BSS task_t *g_tid_prev;  // 当前正在运行的任务
PCPU_BSS task_t *g_tid_next;  // 下次中断将要切换的任务




// 改变任务状态的函数也是在任务中调用，要注意改变当前任务状态的情况

// 调用下面的函数，需要 caller 已经持有 task->spin

// sched 函数只负责更新就绪队列，不会切换任务，也不会发送 IPI


// 挑选负载最轻的，优先级最低的 CPU
static int select_cpu(task_t *task) {
    ASSERT(NULL != task);
    ASSERT(task->spin.ticket_counter > task->spin.service_counter);

    int lowest_cpu = 0;
    int lowest_tick = 0; // 就绪队列总负载
    int lowest_pri = 0; // 优先级

    // 优先选择之前运行的 CPU，缓存无需预热
    if (task->last_cpu >= 0) {
        ASSERT(task->affinity < 0);
        lowest_cpu = task->last_cpu;
        ready_q_t *q = pcpu_ptr(lowest_cpu, &g_ready_q);
        int key = irq_spin_take(&q->spin);
        lowest_tick = q->total_tick;
        lowest_pri = __builtin_ctz(q->priorities);
        irq_spin_give(&q->spin, key);
    }

    for (int i = 0; i < cpu_count(); ++i) {
        if (task->last_cpu == i) {
            continue;
        }

        ready_q_t *q = pcpu_ptr(i, &g_ready_q);
        int key = irq_spin_take(&q->spin);
        int tick = q->total_tick;
        int pri = __builtin_ctz(q->priorities);
        irq_spin_give(&q->spin, key);

        if (tick < lowest_tick) {
            lowest_cpu = i;
            lowest_tick = tick;
            lowest_pri = pri;
        } else if ((tick == lowest_tick) && (pri < lowest_pri)) {
            lowest_cpu = i;
            lowest_pri = pri;
        }
    }

    return lowest_cpu;
}




// 因为某些原因停止任务执行，从就绪队列中移除
uint16_t sched_stop(task_t *task, uint16_t bits) {
    ASSERT(NULL != task);
    ASSERT(task->spin.ticket_counter > task->spin.service_counter);
    ASSERT(0 != bits);

    uint16_t old_state = task->state;
    task->state |= bits;

    // 如果 last_cpu < 0，表示该任务从未运行过
    if ((TASK_READY != old_state) || (task->last_cpu < 0)) {
        return old_state;
    }

    // 将任务从就绪队列中删除，同时更新 tid_next
    ready_q_t *q = pcpu_ptr(task->last_cpu, &g_ready_q);
    int key = irq_spin_take(&q->spin);
    ready_q_remove(q, task);
    task_t *head = ready_q_head(q);
    *(task_t **)pcpu_ptr(task->last_cpu, &g_tid_next) = head;
    irq_spin_give(&q->spin, key);

    return old_state;
}


// 取消 stoped 状态，如果变为就绪，就把任务放入就绪队列
// TODO 应该返回修改之后的状态，而不是之前的
uint16_t sched_cont(task_t *task, uint16_t bits) {
    ASSERT(NULL != task);
    ASSERT(task->spin.ticket_counter > task->spin.service_counter);
    ASSERT(0 != bits);

    uint16_t old_state = task->state;
    task->state &= ~bits;
    if (TASK_READY != task->state) {
        return old_state;
    }

    // 变为就绪态，放入一个就绪队列
    if (task->affinity >= 0) {
        task->last_cpu = task->affinity;
    } else {
        task->last_cpu = select_cpu(task);
        ASSERT(task->last_cpu >= 0);
        ASSERT(task->last_cpu < cpu_count());
        // task->last_cpu = cpu_index();
    }
    ready_q_t *q = pcpu_ptr(task->last_cpu, &g_ready_q);

    int key = irq_spin_take(&q->spin);
    ready_q_insert(q, task);
    task_t *head = ready_q_head(q);
    *(task_t **)pcpu_ptr(task->last_cpu, &g_tid_next) = head;
    irq_spin_give(&q->spin, key);

    // if ((&g_shell_tcb == task) && (head != task)) {
    //     klog("[nopreempt]");
    //     arch_send_stopall();
    // }

    return old_state;
}


#if 1

// 每次时钟中断里执行（但是可能重入）
// 首先锁住就绪队列，防止轮转过程中插入新的任务，导致当前任务不再是最高优先级
void sched_tick() {
    ready_q_t *q = this_ptr(&g_ready_q);
    int key = irq_spin_take(&q->spin);

    // 读取 tid_next，而不是 tid_prev
    // 如果不发生中断，tid_prev 就一致不更新
    // 因为 tid_prev 可能尚未更新，导致我们轮转的不是最高优先级任务
    task_t *curr = THISCPU_GET(g_tid_next);
    raw_spin_take(&curr->spin);

    --curr->tick;
    if (0 == curr->tick) {
        curr->tick = curr->tick_reload;
        task_t *next = containerof(curr->q_node.next, task_t, q_node);
        THISCPU_SET(g_tid_next, next);
    }

    raw_spin_give(&curr->spin);
    irq_spin_give(&q->spin, key);
}

#else

void sched_tick() {
    task_t *self = THISCPU_GET(g_tid_prev);
    int key = irq_spin_take(&self->spin);
    --self->tick;
    if (0 != self->tick) {
        irq_spin_give(&self->spin, key);
        return;
    }

    self->tick = self->tick_reload;

    // 刚才修改 tick 时候，仅仅锁住了任务，没有锁住就绪队列
    // 就绪队列中可能放入了新任务，导致 self 不再是最高优先级
    // 锁住就绪队列（和 tid_next）后，需要判断 tid_next 有无发生变化
    // 如果有变化，说明有抢占，不能再轮转

    ready_q_t *q = this_ptr(&g_ready_q);
    raw_spin_take(&q->spin);
    task_t *next = containerof(self->q_node.next, task_t, q_node);
    if (THISCPU_GET(g_tid_next) == self) {
        THISCPU_SET(g_tid_next, next);
    }

    raw_spin_give(&q->spin);
    irq_spin_give(&self->spin, key);
}

#endif


//------------------------------------------------------------------------------
// 调度器初始化
//------------------------------------------------------------------------------

// 空闲任务
static PCPU_BSS task_t g_idle_tcb;
static shell_cmd_t g_cmd_sched;

// 空闲任务，优先级最低，用于填充 CPU 时间
static NORETURN void idle_proc() {
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

// 显示当前就绪任务
static int sched_show(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    for (int i = 0; i < cpu_count(); ++i) {
        klog("content of ready queue %d:\n", i);
        ready_q_t *q = pcpu_ptr(i, &g_ready_q);
        for (int p = 0; p < PRIORITY_NUM; ++p) {
            if (0 == (q->priorities & (1U << p))) {
                continue;
            }

            dlnode_t *dl = q->heads[p];
            do {
                task_t *task = containerof(dl, task_t, q_node);
                dl = dl->next;
                klog("  - task `%s`, priority %d, affinity %d\n",
                    task->name, task->priority, task->affinity);
            } while (dl != q->heads[p]);
        }
    }

    return 0;
}


// 初始化就绪队列，创建 idle 任务
INIT_TEXT void sched_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        ready_q_t *q = pcpu_ptr(i, &g_ready_q);
        ready_q_init(q);

        task_t *idle = pcpu_ptr(i, &g_idle_tcb);
        task_create_ex(idle, strmake("idle-%d", i), PRIORITY_NUM - 1, i,
                NULL, NULL, IDLE_STACK_RANK, idle_proc, 0, 0, 0, 0);

        // 将idle任务放入就绪队列，但是暂时不切换
        raw_spin_take(&q->spin);
        ready_q_insert(q, idle);
        *(task_t **)pcpu_ptr(i, &g_tid_prev) = NULL;
        *(task_t **)pcpu_ptr(i, &g_tid_next) = idle;
        raw_spin_give(&q->spin);
    }

    g_cmd_sched.name = "ready";
    g_cmd_sched.func = sched_show;
    shell_add_cmd(&g_cmd_sched);
}
