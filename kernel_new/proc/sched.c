#include "sched.h"
#include <library/sched_list.h>
#include <arch_intf.h>
#include <arch_impl.h>
#include <library/dllist.h>
#include <library/string.h>
#include <library/debug.h>
#include <library/spin.h>
#include <memory/vmspace.h>



// // 按优先级排序的有序队列，可用于就绪队列和阻塞队列
// typedef struct priority_q {
//     uint32_t    priorities; // 优先级mask
//     dlnode_t   *heads[PRIORITY_NUM];
//     spin_t      spin;
//     int         load;
//     int         new_task; // 有新任务，可能不再是最低优先级
// } sched_list_t;


typedef struct ready_q {
    sched_list_t sl;
    spin_t spin;
    int load;
} ready_q_t;

static PERCPU_BSS ready_q_t g_ready_q;

static volatile int g_lowest_load;
static volatile int g_lowest_cpu;

static INIT_BSS task_t g_dummy_task;
static PERCPU_BSS task_t g_idle_task;

PERCPU_DATA task_t *g_tid_prev = &g_dummy_task; // 正在运行的任务，只有中断可以更新这个字段
PERCPU_BSS task_t *g_tid_next; // 即将运行的任务，中断返回时切换


//------------------------------------------------------------------------------
// 任务调度
//------------------------------------------------------------------------------

// 停止当前任务
task_t *sched_stop(uint16_t bits) {
    ASSERT(0 != bits);
    ASSERT(0 == cpu_int_depth());

    task_t *tid = THISCPU_GET(g_tid_prev);
    tid->state |= bits;

    ready_q_t *q = thiscpu_ptr(&g_ready_q);
    int key = irq_spin_take(&q->spin);
    ASSERT(sched_list_contains(&q->sl, tid->priority, &tid->q_node));
    q->load -= tid->tick_reload;
    sched_list_remove(&q->sl, tid->priority, &tid->q_node);
    task_t *next = containerof(sched_list_head(&q->sl), task_t, q_node);
    THISCPU_SET(g_tid_next, next);
    irq_spin_give(&q->spin, key);

    return tid;
}

// 将任务放在指定 CPU 上运行
static int sched_cont_on(task_t *tid, uint16_t bits, int cpu) {
    ASSERT(NULL != tid);
    ASSERT(0 != bits);
    ASSERT(TASK_READY != tid->state);

    tid->state &= ~bits;
    if (TASK_READY != tid->state) {
        return -1;
    }

    ready_q_t *q = percpu_ptr(cpu, &g_ready_q);
    int key = irq_spin_take(&q->spin);
    q->load += tid->tick_reload;
    sched_list_insert(&q->sl, tid->priority, &tid->q_node);
    task_t *next = containerof(sched_list_head(&q->sl), task_t, q_node);
    *(task_t **)percpu_ptr(cpu, &g_tid_next) = next;
    irq_spin_give(&q->spin, key);

    return cpu;
}

// 返回选中的 CPU 编号
int sched_cont(task_t *tid, uint16_t bits) {
    return sched_cont_on(tid, bits, g_lowest_cpu);
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
        // g_lowest_load = THISCPU_GET(g_ready_q.load);
        g_lowest_load = ((ready_q_t *)thiscpu_ptr(&g_ready_q))->load;
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
    ready_q_t *q = thiscpu_ptr(&g_ready_q);
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
}


//------------------------------------------------------------------------------
// 初始化
//------------------------------------------------------------------------------

static void idle_proc() {
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
        ready_q_t *q = percpu_ptr(i, &g_ready_q);
        sched_list_init(&q->sl);
        spin_init(&q->spin);
        q->load = 0;

        task_t *idle = percpu_ptr(i, &g_idle_task);
        task_create(idle, "idle", PRIORITY_NUM - 1, 100, idle_proc, 0,0,0,0);
        sched_cont_on(idle, TASK_STOPPED, i);
    }
}
