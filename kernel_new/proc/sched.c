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

    // 任务可能迁移，必须先锁住中断再获取就绪队列
    int key = cpu_int_lock();

    task_t *tid = THISCPU_GET(g_tid_prev);
    tid->state |= bits;
    priority_q_t *q = thiscpu_ptr(&g_ready_q);
    ASSERT(priority_q_contains(q, tid));

    priority_q_remove(q, tid);
    THISCPU_SET(g_tid_next, priority_q_head(q));

    cpu_int_unlock(key);
    return tid;
}

void sched_cont(task_t *tid, uint16_t bits) {
    ASSERT(NULL != tid);
    ASSERT(0 != bits);
    ASSERT(TASK_READY != tid->state);

    tid->state &= ~bits;
    if (TASK_READY != tid->state) {
        return;
    }

    // 选择一个负载最低的 CPU
    // g_lowest_cpu
    priority_q_t *q = thiscpu_ptr(&g_ready_q);

    priority_q_push(q, tid);
    THISCPU_SET(g_tid_next, priority_q_head(q));
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
    arch_task_switch();
}


// 将任务放进就绪队列，参与调度
// 放在当前 CPU 的就绪队列上
void task_resume(task_t *tid) {
    ASSERT(NULL != tid);

    tid->state &= ~TASK_STOPPED;
    if (TASK_READY != tid->state) {
        return;
    }

    priority_q_t *q = thiscpu_ptr(&g_ready_q);
    ASSERT(!priority_q_contains(q, tid));

    int key = irq_spin_take(&q->spin);
    priority_q_push(q, tid); // 放入队列
    tid = priority_q_head(q); // 可能抢占
    THISCPU_SET(g_tid_next, tid);
    irq_spin_give(&q->spin, key);
}

void start_on_lowest(task_t *tid) {
    // TODO 找出 load 最低的 CPU，在那个 CPU 上注册一个异步任务
    // 可以通过发送 IPI 让另一个 CPU 执行代码
}


//------------------------------------------------------------------------------
// 核心调度逻辑
//------------------------------------------------------------------------------

// 执行调度，在时钟中断里执行
void sched_advance() {
    ASSERT(cpu_int_depth());

    priority_q_t *q = thiscpu_ptr(&g_ready_q);

    // 锁住当前队列，防止 tid_next 改变
    // int key = irq_spin_take(&q->spin);

    task_t *tid = THISCPU_GET(g_tid_next);
    if (0 == --tid->tick) {
        tid->tick = tid->tick_reload;
        tid = containerof(tid->q_node.next, task_t, q_node);
        THISCPU_SET(g_tid_next, tid);
    }

    // 检查当前 CPU 是不是负载最低的
    if (q->load < g_lowest_load){
        g_lowest_load = q->load;
        g_lowest_cpu = cpu_index();
    }
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
