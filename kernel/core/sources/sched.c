#include <sched.h>
#include <wheel.h>


//------------------------------------------------------------------------------
// 就绪队列
//------------------------------------------------------------------------------

typedef struct ready_q {
    uint32_t priorities_mask; // 表示该队列包含那些优先级
    dlnode_t heads[PRIORITY_NUM];
} ready_q_t;

static INIT_TEXT void ready_q_init(ready_q_t *q) {
    ASSERT(NULL != q);

    q->priorities_mask = 0;
    for (int i = 0; i < PRIORITY_NUM; ++i) {
        dl_init_circular(&q->heads[i]);
    }
}

// 返回就绪队列中优先级最高的任务
static task_t *ready_q_head(ready_q_t *q) {
    ASSERT(NULL != q);

    int top = __builtin_ctz(q->priorities_mask);
    return containerof(q->heads[top].next, task_t, q_node);
}

static void ready_q_insert(ready_q_t *q, task_t *task) {
    ASSERT(NULL != q);
    ASSERT(NULL != task);

    q->priorities_mask |= 1U << task->priority;
    dl_insert_before(&task->q_node, &q->heads[task->priority]);
}

static void ready_q_remove(ready_q_t *q, task_t *task) {
    ASSERT(NULL != q);
    ASSERT(NULL != task);
    ASSERT(dl_contains(&q->heads[task->priority], &task->q_node));

    dl_remove(&task->q_node);
    if (dl_is_lastone(&q->heads[task->priority])) {
        q->priorities_mask &= ~(1U << task->priority);
    }
}



//------------------------------------------------------------------------------
// 多核任务调度，任务执行状态控制
//------------------------------------------------------------------------------

static PCPU_BSS ready_q_t g_ready_q; // 就绪队列
static int g_lowest_cpu;  // 哪个 CPU 的当前优先级最低
PCPU_BSS task_t *g_tid_prev;  // 当前正在运行的任务
PCPU_BSS task_t *g_tid_next;  // 下次中断将要切换的任务

// 改变任务状态的函数也是在任务中调用，要注意改变当前任务状态的情况

void sched_stop(task_t *task) {
    ASSERT(NULL != task);

    uint16_t old_state = task->state;
    task->state |= TASK_STOPPED;

    if (TASK_READY == old_state) {
        // TODO 还要找出任务在哪个 CPU 上运行
        ready_q_remove(&g_ready_q, task);
    }
}

// 取消 stoped 状态，如果变为就绪，就把任务放入就绪队列
void sched_resume(task_t *task) {
    ASSERT(NULL != task);
    ASSERT(TASK_STOPPED & task->state);

    task->state &= ~TASK_STOPPED;
    if (TASK_READY != task->state) {
        return;
    }

    ready_q_t *q;
    if (task->affinity >= 0) {
        q = pcpu_ptr(task->affinity, &g_ready_q);
    } else {
        // TODO 选择优先级最低的 CPU，而不是当前 CPU
        q = this_ptr(&g_ready_q);
    }
    ready_q_insert(q, task);

    // TODO 判断能否抢占，如果能，则发送 IPI，触发中断切换任务
}


//------------------------------------------------------------------------------
// 调度器初始化
//------------------------------------------------------------------------------

// 空闲任务
static PCPU_BSS task_t idle_task;

static PCPU_BSS uint8_t idle_stack[IDLE_STACK_SIZE];

// 空闲任务，优先级最低，用于填充 CPU 时间
static NORETURN void idle_proc() {
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

// 准备调度子系统
// 初始化就绪队列，创建 idle 任务
// idle task 无需动态分配栈，预留很小的空间即可
INIT_TEXT void sched_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        ready_q_init(pcpu_ptr(i, &g_ready_q));

        task_t *idle = pcpu_ptr(i, &idle_task);
        uint8_t *top = pcpu_ptr(i, &idle_stack[IDLE_STACK_SIZE]);

        task_create_ex(idle, "idle", PRIORITY_NUM - 1, i, NULL,
                top, 0, idle_proc, 0, 0, 0, 0);
        sched_resume(idle);
    }
}
