#include "task.h"
#include <spin.h>
#include <kstring.h>
#include <debug.h>

// 定时任务、调度

// 任务管理控制任务的生命周期，状态切换
// 调度管理 ready 状态的任务

// 任务调度
PERCPU_BSS task_t *g_prev_task;
PERCPU_BSS task_t *g_next_task;
static INIT_BSS task_t g_dummy_task;
static PERCPU_BSS task_t g_idle_task; // 这也是 ready-q 的头节点
// static PERCPU_BSS uint8_t g_idle_stack[1024]; // TODO percpu 负责划分


// 就绪任务队列
static PERCPU_BSS rdyq_t g_rdy_queue;
// static PERCPU_BSS spin_t g_sched_lock;
// static PERCPU_BSS int    g_queue_size;

// 记录哪个 CPU 负载最高最低
// 用于创建任务时选择一个目标 CPU
// 用于 idle 时从其他 CPU 迁移任务
static spin_t g_load_lock;
static int g_lowest_load_cpu = 0;
static int g_highest_load_cpu = 0;

// TODO 负载均衡锁需要支持 read-write lock
// 每个 cpu 执行 reschedule 时，获取 read-lock，多个 reader 可以共存
// 某个 cpu 执行到 idle，获取 writer-lock，独占临界区，检查其他 cpu 的就绪队列




//------------------------------------------------------------------------------
// ready queue
//------------------------------------------------------------------------------

// 就绪队列只关心队列，不管自旋锁

INIT_TEXT void rdyq_init(rdyq_t *q) {
    kmemset(q, 0, sizeof(rdyq_t));
}

void rdyq_insert(rdyq_t *q, dlnode_t *dl, int prio) {
    if (NULL == q->heads[prio]) {
        dl_init_circular(dl);
        q->heads[prio] = dl;
        q->priorities |= 1U << prio;
    } else {
        dl_insert_before(dl, q->heads[prio]);
    }
}

void rdyq_remove(rdyq_t *q, dlnode_t *dl, int prio) {
    if (dl_is_lastone(dl)) {
        ASSERT(q->heads[prio] == dl);
        q->heads[prio] = NULL;
        q->priorities &= ~(1U << prio);
    } else {
        if (dl == q->heads[prio]) {
            q->heads[prio] = dl->next;
        } else {
            ASSERT(dl_contains(q->heads[prio], dl));
        }
        dl_remove(dl);
    }
}

dlnode_t *rdyq_head(rdyq_t *q) {
    int prio = __builtin_ctz(q->priorities);
    return q->heads[prio];
}

// dlnode_t *rdyq_rotate(rdyq_t *q UNUSED, dlnode_t *dl) {
//     return dl->next;
// }


//------------------------------------------------------------------------------
// scheduler
//------------------------------------------------------------------------------

// 任务状态切换
// 管理就绪队列

static NORETURN void idle_proc(task_t *self UNUSED) {
    // logk("%s task running on cpu-%d...\n", self->name, cpu_index());
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

void sched_init() {
    g_dummy_task.name = "temp-dummy-TCB";

    // spin_init(THISCPU(&g_sched_lock));
    // THISCPU_SET(g_queue_size, 1);

    task_t *idle = THISCPU(&g_idle_task);
    // uint8_t *top = THISCPU(g_idle_stack + sizeof(g_idle_stack));
    task_create(idle, "idle", 31, idle_proc);
    dl_init_circular(&idle->dl);

    rdyq_t *q = THISCPU(&g_rdy_queue);
    rdyq_init(q);
    rdyq_insert(q, &idle->dl, 31);

    THISCPU_SET(g_prev_task, &g_dummy_task);
    THISCPU_SET(g_next_task, idle);
}

// run in ISR
// 在同一个优先级的任务之间轮转
void sched_process() {
    // spin_t *lock = THISCPU(&g_sched_lock);
    // raw_spin_take(lock);

    task_t *task = THISCPU_GET(g_next_task);
    task_t *next = containerof(task->dl.next, task_t, dl);
    if (31 == next->priority) {
        // TODO steal task from other cpu
        //      只能找到 idle task，说明当前 readyq 为空
        //      从其他 CPU 寻找 ready task，迁移到这个 CPU
    }
    THISCPU_SET(g_next_task, next);

    // raw_spin_give(lock);
}

// 停止的任务必须位于当前 CPU，要么
void sched_stop(task_t *task, uint32_t bits) {
    if (TS_READY != task->state) {
        task->state |= bits;
        return;
    }

    // remove from ready-queue
    rdyq_t *q = THISCPU(&g_rdy_queue);
    rdyq_remove(q, &task->dl, task->priority);
    task->state |= bits;

    dlnode_t *head = rdyq_head(q);
    task_t *next = containerof(head, task_t, dl);
    THISCPU_SET(g_next_task, next);
}

// 不能在 ISR 里面执行
task_t *sched_stop_self(uint32_t bits) {
    ASSERT(cpu_int_depth() == 0);

    task_t *self = THISCPU_GET(g_prev_task);
    rdyq_t *q = THISCPU(&g_rdy_queue);
    rdyq_remove(q, &self->dl, self->priority);
    self->state |= bits;

    dlnode_t *head = rdyq_head(q);
    task_t *next = containerof(head, task_t, dl);
    THISCPU_SET(g_next_task, next);

    return self;
}

// 在当前 CPU 恢复运行这个 task
// 但不要立即触发 task-switch
void sched_cont(task_t *task, uint32_t bits) {
    ASSERT(TS_READY != task->state);
    task->state &= ~bits;
    if (TS_READY != task->state) {
        return; // still no ready
    }

    rdyq_t *q = THISCPU(&g_rdy_queue);
    rdyq_insert(q, &task->dl, task->priority);
    task_t *next = containerof(rdyq_head(q), task_t, dl);
    THISCPU_SET(g_next_task, next);
}


//------------------------------------------------------------------------------
// task management
//------------------------------------------------------------------------------

// // 新任务的默认执行入口，该函数不能返回
// static NORETURN void task_entry(task_t *self) {
//     logk("starting task %s\n", self->name);
//     // TODO 在这里调用
//     self->entry();
//     logk("stopping task %s\n", self->name);

//     while (1) {
//         cpu_pause();
//         cpu_halt();
//     }
// }

void task_create_ex(task_t *task, const char *name, int priority, size_t stack_top, void *entry) {
    task->name = name;
    task->priority = priority;
    task->state = TS_STOPPED;
    arch_task_init(task, (size_t)entry, (size_t)stack_top);
}

void task_create(task_t *task, const char *name, int priority, void *entry) {
    // 必须分配足够大的栈，如果执行 logk，对栈的使用很大
    // TODO 不应该允许用户自己指定栈顶地址，必须动态分配页，动态映射，这样越界容易发现
    task->stack.desc = name;
    size_t stack_va = vmspace_alloc_stack(&g_kernel_vm, &task->stack, 0);
    // logk("alloc stack for %s, va %zx, pa %zx\n", name, stack_va, task->stack.paddr);
    size_t stack_top = stack_va + PAGE_SIZE;
    task_create_ex(task, name, priority, stack_top, entry);
}

void task_start(task_t *task) {
    sched_cont(task, TS_STOPPED);
}
