#include <sched.h>
#include <wheel.h>

#include <spin.h>


//------------------------------------------------------------------------------
// 就绪队列
//------------------------------------------------------------------------------

// 我们的双链表中包含头节点，

typedef struct ready_q {
    spin_t    spin;
    uint32_t  priorities_mask; // 表示该队列包含那些优先级
    dlnode_t *heads[PRIORITY_NUM];
} ready_q_t;

static INIT_TEXT void ready_q_init(ready_q_t *q) {
    ASSERT(NULL != q);

    q->spin = SPIN_INIT;
    q->priorities_mask = 0;
    memset(q->heads, 0, PRIORITY_NUM * sizeof(dlnode_t *));
}

// 调用下面的函数，需要 caller 已经获取就绪队列的自旋锁

// 返回就绪队列中优先级最高的任务
static task_t *ready_q_head(ready_q_t *q) {
    ASSERT(NULL != q);
    ASSERT(q->spin.ticket_counter > q->spin.service_counter);

    int top = __builtin_ctz(q->priorities_mask);
    ASSERT(NULL != q->heads[top]);
    return containerof(q->heads[top], task_t, q_node);
}

static void ready_q_insert(ready_q_t *q, task_t *task) {
    ASSERT(NULL != q);
    ASSERT(q->spin.ticket_counter > q->spin.service_counter);
    ASSERT(NULL != task);

    int pri = task->priority;

    if ((1U << pri) & q->priorities_mask) {
        ASSERT(NULL != q->heads[pri]);
        dl_insert_before(&task->q_node, q->heads[pri]);
    } else {
        ASSERT(NULL == q->heads[pri]);
        dl_init_circular(&task->q_node);
        q->heads[pri] = &task->q_node;
        q->priorities_mask |= 1U << pri;
    }
}

static void ready_q_remove(ready_q_t *q, task_t *task) {
    ASSERT(NULL != q);
    ASSERT(q->spin.ticket_counter > q->spin.service_counter);
    ASSERT(NULL != task);

    int pri = task->priority;
    ASSERT(NULL != q->heads[pri]);
    ASSERT(q->priorities_mask & (1U << pri));

    dlnode_t *next = dl_remove(&task->q_node);

    if (NULL == next) {
        q->heads[pri] = NULL;
        q->priorities_mask &= ~(1U << pri);
        return;
    }
    if (q->heads[pri] == &task->q_node) {
        q->heads[pri] = next;
    }
}



//------------------------------------------------------------------------------
// 多核任务调度，任务执行状态控制
//------------------------------------------------------------------------------

static PCPU_BSS ready_q_t g_ready_q; // 就绪队列
// static int g_lowest_cpu;  // 哪个 CPU 的当前优先级最低

// 在这里 g_tid_prev 是只读的，只有 ISR（entries.S）能更新这个变量
// g_tid_next 也受 g_ready_q->spin 的控制，可以将其看作就绪队列的成员
PCPU_BSS task_t *g_tid_prev;  // 当前正在运行的任务
PCPU_BSS task_t *g_tid_next;  // 下次中断将要切换的任务




// 改变任务状态的函数也是在任务中调用，要注意改变当前任务状态的情况

// 调用下面的函数，需要 caller 已经持有 task->spin

// sched 函数只负责更新就绪队列，不会切换任务，也不会发送 IPI




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
    ready_q_t *q;
    if (task->affinity >= 0) {
        q = pcpu_ptr(task->affinity, &g_ready_q);
        task->last_cpu = task->affinity;
    } else {
        // TODO 选择优先级最低的 CPU，而不是当前 CPU
        q = this_ptr(&g_ready_q);
        task->last_cpu = cpu_index();
    }

    int key = irq_spin_take(&q->spin);
    ready_q_insert(q, task);
    task_t *head = ready_q_head(q);
    *(task_t **)pcpu_ptr(task->last_cpu, &g_tid_next) = head;
    irq_spin_give(&q->spin, key);

    return old_state;
}


// 轮转，切换到同优先级的另一个任务
// 当前任务耗尽时间片时调用，中断内执行
void sched_rotate() {
    ready_q_t *q = this_ptr(&g_ready_q);

    // 虽然处于中断，但中断也会重入

    int key = irq_spin_take(&q->spin);
    task_t *prev = THISCPU_GET(g_tid_prev);
    task_t *next = containerof(prev->q_node.next, task_t, q_node);
    THISCPU_SET(g_tid_next, next);
    irq_spin_give(&q->spin, key);
}


// 每次时钟中断里执行（但是可能重入）
void sched_tick() {
    task_t *prev = THISCPU_GET(g_tid_prev);
    --prev->tick;
    if (0 != prev->tick) {
        return;
    }

    prev->tick = prev->tick_reload;
    sched_rotate();
}


//------------------------------------------------------------------------------
// 调度器初始化
//------------------------------------------------------------------------------

// 空闲任务
static PCPU_BSS task_t idle_tcb;

// static PCPU_BSS uint8_t idle_stack[IDLE_STACK_SIZE];

// 空闲任务，优先级最低，用于填充 CPU 时间
static NORETURN void idle_proc() {
    // klog("cpu %d begin idling\n", cpu_index());
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

// 初始化就绪队列，创建 idle 任务
INIT_TEXT void sched_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        ready_q_t *q = pcpu_ptr(i, &g_ready_q);
        ready_q_init(q);

        task_t *idle = pcpu_ptr(i, &idle_tcb);
        task_create_ex(idle, "idle", PRIORITY_NUM - 1, i, NULL,
                NULL, IDLE_STACK_RANK, idle_proc, 0, 0, 0, 0);

        // 将idle任务放入就绪队列，但是暂时不切换
        raw_spin_take(&q->spin);
        ready_q_insert(q, idle);
        *(task_t **)pcpu_ptr(i, &g_tid_prev) = NULL;
        *(task_t **)pcpu_ptr(i, &g_tid_next) = idle;
        raw_spin_give(&q->spin);

        raw_spin_take(&idle->spin);
        // sched_cont(idle, TASK_STOPPED);
        // 不释放 idle_task 的自旋锁，禁止后续对 idle-task 操作
    }
}
