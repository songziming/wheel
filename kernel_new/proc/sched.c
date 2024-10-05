#include "sched.h"
#include <arch_intf.h>
#include <arch_impl.h>
#include <library/dllist.h>
#include <library/string.h>
#include <library/debug.h>
#include <library/spin.h>
#include <memory/vmspace.h>



// 按优先级排序的有序队列，可用于就绪队列和阻塞队列
typedef struct priority_q {
    uint32_t    priorities; // 优先级mask
    dlnode_t   *heads[PRIORITY_NUM];
    spin_t      spin;
    int         load;
    int         new_task; // 有新任务，可能不再是最低优先级
} priority_q_t;


static PERCPU_BSS priority_q_t g_ready_q;

static volatile int g_lowest_load;
static volatile int g_lowest_cpu;

PERCPU_BSS task_t *g_tid_prev; // 正在运行的任务，只有中断可以更新这个字段
PERCPU_BSS task_t *g_tid_next; // 即将运行的任务，中断返回时切换


//------------------------------------------------------------------------------
// 任务队列
//------------------------------------------------------------------------------

// 可用于就绪队列、阻塞队列

static void priority_q_init(priority_q_t *q) {
    memset(q, 0, sizeof(priority_q_t));
}

static void priority_q_push(priority_q_t *q, task_t *tid) {
    ASSERT(NULL != q);
    ASSERT(NULL != tid);

    int pri = tid->priority;

    if ((1U << pri) & q->priorities) {
        dl_insert_before(&tid->q_node, q->heads[pri]);
    } else {
        dl_init_circular(&tid->q_node);
        q->heads[pri] = &tid->q_node;
        q->priorities |= 1U << pri;
    }

    q->load += tid->tick_reload;
}

static int priority_q_contains(priority_q_t *q, task_t *tid) {
    ASSERT(NULL != q);
    ASSERT(NULL != tid);

    int pri = tid->priority;
    if ((1U << pri) & q->priorities) {
        dlnode_t *head = q->heads[pri];
        dlnode_t *node = &tid->q_node;
        return (head == node) || dl_contains(head, node);
    }

    return 0;
}

static task_t *priority_q_head(priority_q_t *q) {
    ASSERT(NULL != q);
    if (0 == q->priorities) {
        return NULL;
    }
    int top = __builtin_ctz(q->priorities);
    ASSERT(NULL != q->heads[top]);
    return containerof(q->heads[top], task_t, q_node);
}

static void priority_q_remove(priority_q_t *q, task_t *tid) {
    ASSERT(NULL != q);
    ASSERT(NULL != tid);
    ASSERT(priority_q_contains(q, tid));
    ASSERT(q->load >= tid->tick_reload);

    int pri = tid->priority;
    ASSERT((1U << pri) & q->priorities);

    q->load -= tid->tick_reload;

    if (dl_is_lastone(&tid->q_node)) {
        ASSERT(q->heads[pri] == &tid->q_node);
        q->heads[pri] = NULL;
        q->priorities &= ~(1U << pri);
        return;
    }

    dlnode_t *next = dl_remove(&tid->q_node);
    if (q->heads[pri] == &tid->q_node) {
        q->heads[pri] = next;
    }
}

// // 遍历每个 CPU 的就绪队列，找出优先级最低的 CPU
// static int lowest_cpu() {
//     for (int i = 0; i < cpu_count(); ++i) {
//         priority_q_t *q = percpu_ptr(i, &g_ready_q);
//     }
//     return 0;
// }



// 当前 CPU 的就绪队列已更新，可能变为负载最低的 CPU
// TODO 如果负载增加，需要查询其他 CPU 的就绪队列，但那样需要加锁
//      不妨使用一个全局变量 g_needs_update_lowest，每个 CPU 在时钟函数中检查这个变量
//      如果发生变化，说明 lowest 已失效，需要重新设置。
// void update_lowest() {
//     priority_q_t *q = thiscpu_ptr(&g_ready_q);
// }

// int get_lowest() {
//     //
// }


//------------------------------------------------------------------------------
// 任务调度
//------------------------------------------------------------------------------

// 停止当前任务
task_t *sched_stop(uint16_t bits) {
    ASSERT(0 != bits);
    ASSERT(0 == cpu_int_depth());

    task_t *tid = THISCPU_GET(g_tid_prev);
    tid->state |= bits;

    priority_q_t *q = thiscpu_ptr(&g_ready_q);
    int key = irq_spin_take(&q->spin);
    ASSERT(priority_q_contains(q, tid));
    priority_q_remove(q, tid);
    THISCPU_SET(g_tid_next, priority_q_head(q));
    irq_spin_give(&q->spin, key);

    return tid;
}

// 将任务放在指定 CPU 上运行
void sched_cont_on(task_t *tid, uint16_t bits, int cpu) {
    ASSERT(NULL != tid);
    ASSERT(0 != bits);
    ASSERT(TASK_READY != tid->state);

    tid->state &= ~bits;
    if (TASK_READY != tid->state) {
        return;
    }

    priority_q_t *q = percpu_ptr(cpu, &g_ready_q);
    int key = irq_spin_take(&q->spin);
    priority_q_push(q, tid);
    // q->new_task = 1;
    *(task_t **)percpu_ptr(cpu, &g_tid_next) = priority_q_head(q);
    irq_spin_give(&q->spin, key);
}

// 返回选中的 CPU 编号
int sched_cont(task_t *tid, uint16_t bits) {
    int cpu = g_lowest_cpu;
    sched_cont_on(tid, bits, cpu);
    return cpu;
}


//------------------------------------------------------------------------------
// 任务管理
//------------------------------------------------------------------------------

void task_create(task_t *tid, const char *name, uint8_t priority, int tick,
        void *entry, void *arg1, void *arg2, void *arg3, void *arg4) {
    ASSERT(NULL != tid);
    ASSERT(priority < PRIORITY_NUM);

    spin_init(&tid->spin);
    tid->state = TASK_STOPPED; // 初始状态为暂停
    tid->priority = priority;
    tid->tick = tick;
    tid->tick_reload = tick;

    // 分配任务栈
    if (0 == alloc_kernel_stack(&tid->stack, TASK_STACK_RANK)) {
        log("failed to allocate stack space");
        return;
    }

    tid->stack.desc = name;

    arch_task_init(tid, (size_t)entry, arg1, arg2, arg3, arg4);
}

// 任务已经结束，在中断返回阶段执行此函数
static void task_post_delete(void *arg1, void *arg2 UNUSED) {
    task_t *tid = (task_t *)arg1;
    ASSERT(TASK_DELETED & tid->state);
    vmspace_remove(NULL, &tid->stack);
}



// 停止当前正在运行的任务
void task_exit() {
    task_t *tid = sched_stop(TASK_DELETED);
    work_defer(&tid->work, task_post_delete, tid, NULL);

    // 挑选出新的最低负载 CPU
    // TODO 应该换成 compare-and-set，原子性操作
    if (cpu_index() == g_lowest_cpu) {
        g_lowest_load = THISCPU_GET(g_ready_q.load);
    }

    arch_task_switch();
}


// 将任务放进就绪队列，参与调度
// 放在当前 CPU 的就绪队列上
void task_resume(task_t *tid) {
    ASSERT(NULL != tid);

    int cpu = sched_cont(tid, TASK_STOPPED);

    if (cpu_index() != cpu) {
        arch_ipi_resched(cpu);
    } else if (0 == cpu_int_depth()) {
        arch_task_switch();
    }
}


//------------------------------------------------------------------------------
// 核心调度逻辑
//------------------------------------------------------------------------------

// 执行调度，在时钟中断里执行
void sched_advance() {
    ASSERT(cpu_int_depth());

    // 锁住当前队列，防止 tid_next 改变
    priority_q_t *q = thiscpu_ptr(&g_ready_q);
    raw_spin_take(&q->spin);

    task_t *tid = THISCPU_GET(g_tid_next);
    if (0 == --tid->tick) {
        tid->tick = tid->tick_reload;
        tid = containerof(tid->q_node.next, task_t, q_node);
        THISCPU_SET(g_tid_next, tid);
    }

    // 检查当前 CPU 是不是负载最低的
    if (q->load < g_lowest_load) {
        g_lowest_load = q->load;
        g_lowest_cpu = cpu_index();
    }

    raw_spin_give(&q->spin);
    q->new_task = 0;
}


//------------------------------------------------------------------------------
// 初始化
//------------------------------------------------------------------------------

static INIT_BSS task_t g_dummy_task;
static PERCPU_BSS task_t g_idle_task;

static void idle_proc() {
    // cpu_int_lock();
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

INIT_TEXT void sched_init() {
    memset(&g_dummy_task, 0, sizeof(task_t));
    g_dummy_task.stack.desc = "dummy";

    g_lowest_cpu = 0;
    g_lowest_load = INT_MAX;

    for (int i = 0, N = cpu_count(); i < N; ++i) {
        priority_q_t *q = percpu_ptr(i, &g_ready_q);
        priority_q_init(q);
        spin_init(&q->spin);

        task_t *idle = percpu_ptr(i, &g_idle_task);
        task_create(idle, "idle", PRIORITY_IDLE, 100, idle_proc, 0,0,0,0);

        idle->state = TASK_READY;
        priority_q_push(q, idle);

        *(task_t **)percpu_ptr(i, &g_tid_prev) = &g_dummy_task;
        *(task_t **)percpu_ptr(i, &g_tid_next) = idle;
    }
}
