#include <sched.h>
#include <wheel.h>

#include <spin.h>
#include <shell.h>



typedef struct task_q {
    uint32_t  priorities; // 位图，表示队列包含的优先级
    dlnode_t *heads[PRIORITY_NUM];
} task_q_t;

typedef struct ready_q {
    spin_t   spin;
    uint32_t total_tick; // 队列中所有任务的时间片之和
    task_q_t tasks;
} ready_q_t;

typedef struct pend_q {
    spin_t   spin;
    task_q_t tasks;
} pend_q_t;



static PCPU_BSS ready_q_t g_ready_q; // 就绪队列
static PCPU_BSS task_t g_idle_tcb; // 空闲任务

// 在这里 g_tid_prev 是只读的，只有 ISR（entries.S）能更新这个变量
// 就绪队列自旋锁 g_ready_q->spin 同时也控制 g_tid_next（以及每个任务中的 q_node）
PCPU_BSS task_t *g_tid_prev;  // 当前正在运行的任务
PCPU_BSS task_t *g_tid_next;  // 下次中断将要切换的任务

static shell_cmd_t g_cmd_sched;




//------------------------------------------------------------------------------
// 任务队列
//------------------------------------------------------------------------------

static void task_q_push(task_q_t *q, task_t *tid) {
    int pri = tid->priority;

    if ((1U << pri) & q->priorities) {
        ASSERT(NULL != q->heads[pri]);
        dl_insert_before(&tid->q_node, q->heads[pri]);
    } else {
        ASSERT(NULL == q->heads[pri]);
        dl_init_circular(&tid->q_node);
        q->heads[pri] = &tid->q_node;
        q->priorities |= 1U << pri;
    }
}

static task_t *task_q_head(task_q_t *q) {
    if (0 == q->priorities) {
        return NULL;
    }

    int top = __builtin_ctz(q->priorities);
    ASSERT(NULL != q->heads[top]);
    return containerof(q->heads[top], task_t, q_node);
}

static void task_q_remove(task_q_t *q, task_t *tid) {
    int pri = tid->priority;
    ASSERT((1U << pri) & q->priorities);
    ASSERT(NULL != q->heads[pri]);
    ASSERT((&tid->q_node == q->heads[pri]) || dl_contains(q->heads[pri], &tid->q_node));

    if (dl_is_lastone(&tid->q_node)) {
        ASSERT(&tid->q_node == q->heads[pri]);
        q->priorities &= ~(1U << pri);
        q->heads[pri] = NULL;
        return;
    }

    dlnode_t *next = dl_remove(&tid->q_node);
    if (&tid->q_node == q->heads[pri]) {
        q->heads[pri] = next;
    }
}



//------------------------------------------------------------------------------
// 就绪队列管理
//------------------------------------------------------------------------------

// static inline task_t *ready_q_head(ready_q_t *rdy) {
//     return task_q_head(&rdy->tasks);
// }

static inline void ready_q_insert(ready_q_t *rdy, task_t *tid) {
    task_q_push(&rdy->tasks, tid);
    rdy->total_tick += tid->tick_reload;
}

static void ready_q_remove(ready_q_t *rdy, task_t *tid) {
    task_q_remove(&rdy->tasks, tid);
    rdy->total_tick -= tid->tick_reload;
}

// 寻找当前优先级最低的 CPU
// 本函数需要获取多个就绪队列的自旋锁，必须按编号顺序获取
static int lowest_cpu() {
    uint32_t lowest_pri = 0;
    uint32_t lowest_load = 0;
    int idx = 0;

    for (int i = 0; i < cpu_count(); ++i) {
        ready_q_t *q = pcpu_ptr(i, &g_ready_q);

        int key = irq_spin_take(&q->spin);
        uint32_t top = q->tasks.priorities ^ (q->tasks.priorities - 1);
        uint32_t load = q->total_tick;

        if (top > lowest_pri) {
            lowest_pri = top;
            lowest_load = load;
            idx = i;
        } else if ((top == lowest_pri) && (load < lowest_load)) {
            lowest_load = load;
            idx = i;
        }

        irq_spin_give(&q->spin, key);
    }

    return idx;
}

// 下面两个函数是任务状态切换的核心，涉及到就绪队列
// 首先拿到任务的指针，然后拿到就绪队列的指针。先锁住任务，再锁住所在的队列，可能数据竞争。
// TODO 更新了某个就绪队列，不一定发生抢占，可能最高优先级不变，这时不必发送 IPI，可以减少很多不必要的操作

// TODO 我们可以要求，只有自己可以停止任务的执行，不允许停止其他的 task

// 停止任务执行，从就绪队列中移除（目标任务可能位于其他 CPU）
// 同时持有任务和所属就绪队列的自旋锁
// 返回受影响的 CPU 的编号（-1 表示没有影响）
int sched_stop(task_t *tid, uint16_t bits) {
    ASSERT(NULL != tid);
    // ASSERT(tid->spin.ticket_counter > tid->spin.service_counter);
    ASSERT(0 != bits);

    uint16_t state = tid->state;
    int cpu = tid->last_cpu;
    tid->state |= bits;

    // 如果原任务不是就绪态，就不会影响 CPU
    // 如果 last_cpu < 0，表示该任务未被放入就绪队列
    if ((TASK_READY != state) || (cpu < 0)) {
        return -1;
    }

    // 将任务从就绪队列中删除，同时更新 tid_next
    ready_q_t *q = pcpu_ptr(cpu, &g_ready_q);
    int key = irq_spin_take(&q->spin);
    ready_q_remove(q, tid);
    tid->last_cpu = -1;
    *(task_t **)pcpu_ptr(cpu, &g_tid_next) = task_q_head(&q->tasks);
    irq_spin_give(&q->spin, key);

    // 返回有更新的就绪队列编号
    return cpu;
}


// 停止当前任务，从就绪队列取出
// 必须在任务上下文执行，不能在中断上下文执行
task_t *sched_stop_self(uint16_t bits) {
    ASSERT(0 != bits);
    // ASSERT(NULL != pend);
    ASSERT(0 == cpu_int_depth());

    // 当前任务一定处于就绪态
    ready_q_t *q = this_ptr(&g_ready_q);
    task_t *self = THISCPU_GET(g_tid_prev);

    // 锁住了队列，也就锁住了队列的内容
    int key = irq_spin_take(&q->spin);
    self->state |= bits;
    ready_q_remove(q, self);
    self->last_cpu = -1;
    THISCPU_SET(g_tid_next, task_q_head(&q->tasks));
    irq_spin_give(&q->spin, key);

    return self;
}


// 取消 stoped 状态，如果变为就绪，就把任务放入就绪队列
// 返回就绪队列有变更的 CPU 的编号
int sched_cont(task_t *tid, uint16_t bits) {
    ASSERT(NULL != tid);
    // ASSERT(tid->spin.ticket_counter > tid->spin.service_counter);
    ASSERT(0 != bits);

    uint16_t old_state = tid->state;
    tid->state &= ~bits;

    // 如果任务原本就在运行，或者仍然无法运行
    if ((TASK_READY == old_state) || (TASK_READY != tid->state)) {
        return -1;
    }

    // 寻找一个就绪队列，将任务放入
    if (tid->affinity >= 0) {
        tid->last_cpu = tid->affinity;
    } else {
        tid->last_cpu = lowest_cpu();
        ASSERT(tid->last_cpu >= 0);
        ASSERT(tid->last_cpu < cpu_count());
    }
    ready_q_t *q = pcpu_ptr(tid->last_cpu, &g_ready_q);

    int key = irq_spin_take(&q->spin);
    ready_q_insert(q, tid);
    *(task_t **)pcpu_ptr(tid->last_cpu, &g_tid_next) = task_q_head(&q->tasks);
    irq_spin_give(&q->spin, key);

    return tid->last_cpu;
}

// 每次时钟中断里执行（但是可能重入）
// 首先锁住就绪队列，防止轮转过程中插入新的任务，导致当前任务不再是最高优先级
void sched_tick() {
    ASSERT(cpu_int_depth());

    ready_q_t *q = this_ptr(&g_ready_q);
    int key = irq_spin_take(&q->spin);

    // 读取 tid_next，而不是 tid_prev，如果不发生中断，tid_prev 就一直不更新
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



//------------------------------------------------------------------------------
// 阻塞状态管理
//------------------------------------------------------------------------------

#if 0

static void unpend(task_t *tid, UNUSED void *a2) {
    sched_cont(tid, TASK_PENDING);
}

// 将当前任务阻塞在队列上，当前任务一定处于运行状态
void pend_on(pend_q_t *q, int timeout) {
    task_t *tid = THISCPU_GET(g_tid_prev);

    timer_t timer;
    if (timeout) {
        // work_t timer;
        timer_start(&timer, timeout, unpend, tid, NULL);
        klog("setting timer to fire %d ticks later\n", timeout);
    }

    // sched_stop(tid, TASK_PENDING); // 从所在就绪队列中移除，更新 tid_next
    // task_q_push(&q->tasks, tid); // 放进阻塞队列

    arch_task_switch();
}


// 将队列里阻塞的任务全部唤醒
void unpend_all(pend_q_t *q) {
    while (1) {
        task_t *tid = task_q_head(&q->tasks);
        if (NULL == tid) {
            return;
        }

        sched_cont(tid, TASK_PENDING);
        // TODO 记录每个 CPU 是否应该执行 reched ipi
    }
}

#endif


//------------------------------------------------------------------------------
// 多核任务调度，任务执行状态控制
//------------------------------------------------------------------------------




// 改变任务状态的函数也是在任务中调用，要注意改变当前任务状态的情况

// 调用下面的函数，需要 caller 已经持有 tid->spin

// sched 函数只负责更新就绪队列，不会切换任务，也不会发送 IPI


// // 挑选负载最轻的，优先级最低的 CPU
// static int select_cpu(task_t *tid) {
//     ASSERT(NULL != tid);
//     ASSERT(tid->spin.ticket_counter > tid->spin.service_counter);

//     int lowest_cpu = 0;
//     int lowest_tick = 0; // 就绪队列总负载
//     int lowest_pri = 0; // 优先级

//     // 优先选择之前运行的 CPU，缓存无需预热
//     if (tid->last_cpu >= 0) {
//         ASSERT(tid->affinity < 0);
//         lowest_cpu = tid->last_cpu;
//         ready_q_t *q = pcpu_ptr(lowest_cpu, &g_ready_q);
//         int key = irq_spin_take(&q->spin);
//         lowest_tick = q->total_tick;
//         lowest_pri = __builtin_ctz(q->tasks.priorities);
//         irq_spin_give(&q->spin, key);
//     }

//     for (int i = 0; i < cpu_count(); ++i) {
//         if (tid->last_cpu == i) {
//             continue;
//         }

//         ready_q_t *q = pcpu_ptr(i, &g_ready_q);
//         int key = irq_spin_take(&q->spin);
//         int tick = q->total_tick;
//         int pri = __builtin_ctz(q->tasks.priorities);
//         irq_spin_give(&q->spin, key);

//         if (tick < lowest_tick) {
//             lowest_cpu = i;
//             lowest_tick = tick;
//             lowest_pri = pri;
//         } else if ((tick == lowest_tick) && (pri < lowest_pri)) {
//             lowest_cpu = i;
//             lowest_pri = pri;
//         }
//     }

//     return lowest_cpu;
// }




//------------------------------------------------------------------------------
// 调度器初始化
//------------------------------------------------------------------------------

// 空闲任务，优先级最低，用于填充 CPU 时间
static NORETURN void idle_proc() {
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

// 显示当前就绪任务
static int sched_show(UNUSED int argc, UNUSED char *argv[]) {
    for (int i = 0; i < cpu_count(); ++i) {
        klog("content of ready queue %d:\n", i);
        ready_q_t *q = pcpu_ptr(i, &g_ready_q);
        for (int pri = 0; pri < PRIORITY_NUM; ++pri) {
            if (0 == (q->tasks.priorities & (1U << pri))) {
                continue;
            }

            dlnode_t *dl = q->tasks.heads[pri];
            do {
                task_t *tid = containerof(dl, task_t, q_node);
                dl = dl->next;
                klog("  - task `%s`, priority %d, affinity %d\n",
                    tid->name, tid->priority, tid->affinity);
            } while (dl != q->tasks.heads[pri]);
        }
    }

    return 0;
}


// 初始化就绪队列，创建 idle 任务
INIT_TEXT void sched_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        ready_q_t *q = pcpu_ptr(i, &g_ready_q);
        q->spin = SPIN_INIT;
        q->total_tick = 0;
        memset(&q->tasks, 0, sizeof(task_q_t));
        // ready_q_init(q);

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
