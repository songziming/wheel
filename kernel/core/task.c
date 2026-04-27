#include "task.h"
#include <spin.h>
#include <kstring.h>
#include <debug.h>

// 任务管理控制任务的生命周期，状态切换
// 调度管理 ready 状态的任务


static INIT_DATA task_t g_dummy_task = { .name = "dummy-TCB" };
static PERCPU_BSS task_t g_idle_task;

// 就绪任务队列
// static PERCPU_DATA spin_t g_sched_lock;
static PERCPU_BSS rdyq_t g_rdy_queue; // ready-queue
PERCPU_BSS task_t *g_prev_task;
PERCPU_BSS task_t *g_next_task; // also guarded by rdyq->lock


// 负载均衡，寻找负载最低的 cpu
// 所有 cpu 都需要使用这个信息，需要使用锁
static rwspin_t g_balance_lock;
static _Atomic uint64_t g_idle_mask;

// 记录哪个 CPU 负载最高最低
// 用于创建任务时选择一个目标 CPU
// 用于 idle 时从其他 CPU 迁移任务

// TODO 负载均衡锁需要支持 read-write lock
// 每个 cpu 执行 reschedule 时，获取 read-lock，多个 reader 可以共存
// 某个 cpu 执行到 idle，获取 writer-lock，独占临界区，检查其他 cpu 的就绪队列




//------------------------------------------------------------------------------
// ready queue
//------------------------------------------------------------------------------

// 就绪队列只关心队列，不管自旋锁

INIT_TEXT void rdyq_init(rdyq_t *q) {
    q->lock = SPIN_INIT;
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
    rdyq_t *q = THISCPU(&g_rdy_queue);
    rdyq_init(q);

    task_t *idle = THISCPU(&g_idle_task);
    task_create(idle, "idle", 31, idle_proc);
    rdyq_insert(q, &idle->dl, 31);

    atomic_fetch_or(&g_idle_mask, 1U << cpu_index());
    THISCPU_SET(g_prev_task, &g_dummy_task);
    THISCPU_SET(g_next_task, idle);
}

// run in ISR
// 在同一个优先级的任务之间轮转
void sched_process() {
    ASSERT(cpu_int_depth() > 0);

    // 需要锁住当前就绪队列
    rdyq_t *q = THISCPU(&g_rdy_queue);
    int key = irq_spin_take(&q->lock);

    task_t *task = THISCPU_GET(g_prev_task);
    if (TS_READY == task->state) {
        task->tick--;
        if (0 != task->tick) {
            irq_spin_give(&q->lock, key);
            return;
        }
        task->tick = task->tick_reload;
        task = containerof(task->dl.next, task_t, dl);
    } else {
        task = containerof(rdyq_head(q), task_t, dl);
        if (31 == task->priority) {
            // TODO 从其他 CPU 迁移任务
            atomic_fetch_or(&g_idle_mask, 1U << cpu_index());
        }
    }

    THISCPU_SET(g_next_task, task);
    irq_spin_give(&q->lock, key);
}

// // 停止的任务必须位于当前 CPU，要么
// void sched_stop(task_t *task, uint32_t bits) {
//     if (TS_READY != task->state) {
//         task->state |= bits;
//         return;
//     }

//     // remove from ready-queue
//     rdyq_t *q = THISCPU(&g_rdy_queue);
//     rdyq_remove(q, &task->dl, task->priority);
//     task->state |= bits;

//     dlnode_t *head = rdyq_head(q);
//     task_t *next = containerof(head, task_t, dl);
//     THISCPU_SET(g_next_task, next);
// }

// 不能在 ISR 里面执行
task_t *sched_stop_self(uint32_t bits) {
    ASSERT(cpu_int_depth() == 0);

    rdyq_t *q = THISCPU(&g_rdy_queue);
    int key = irq_spin_take(&q->lock);

    task_t *self = THISCPU_GET(g_prev_task);
    rdyq_remove(q, &self->dl, self->priority);
    self->state |= bits;

    dlnode_t *head = rdyq_head(q);
    task_t *next = containerof(head, task_t, dl);
    if (THISCPU(&g_idle_task) == next) {
        atomic_fetch_or(&g_idle_mask, 1U << cpu_index());
    }

    THISCPU_SET(g_next_task, next);
    irq_spin_give(&q->lock, key);

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
    task->tick = task->tick_reload;

    rdyq_t *q = THISCPU(&g_rdy_queue);
    int key = irq_spin_take(&q->lock);

    rdyq_insert(q, &task->dl, task->priority);
    task_t *next = containerof(rdyq_head(q), task_t, dl);
    if (THISCPU_GET(g_next_task) == THISCPU(&g_idle_task)) {
        atomic_fetch_and(&g_idle_mask, ~(1U << cpu_index()));
    }

    THISCPU_SET(g_next_task, next);
    irq_spin_give(&q->lock, key);
}


// 在另一个 cpu 上启动运行任务
void sched_cont_on(task_t *task, uint32_t bits, int cpu) {
    ASSERT(cpu_index() != cpu);

    task->state &= ~bits;
    if (TS_READY != task->state) {
        return; // still no ready
    }
    task->tick = task->tick_reload;

    rdyq_t *q = PERCPU(cpu, &g_rdy_queue);
    int key = irq_spin_take(&q->lock);

    rdyq_insert(q, &task->dl, task->priority);
    task_t *next = containerof(rdyq_head(q), task_t, dl);
    if (*PERCPU(cpu, &g_next_task) == PERCPU(cpu, &g_idle_task)) {
        atomic_fetch_and(&g_idle_mask, ~(1U << cpu));
    }

    *PERCPU(cpu, &g_next_task) = next;
    irq_spin_give(&q->lock, key);
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

void task_create(task_t *task, const char *name, int priority, void *entry) {
    // 必须分配足够大的栈，如果执行 logk，对栈的使用很大
    // 不应该允许用户自己指定栈顶地址，必须动态分配页，动态映射，这样越界容易发现
    size_t stack_va = vmspace_alloc_stack(&g_kernel_vm, &task->stack, 0);
    size_t stack_top = stack_va + PAGE_SIZE;
    task->stack.desc = name;

    task->name = name;
    task->priority = priority;
    task->state = TS_STOPPED;
    task->tick = 10;
    task->tick_reload = 10;
    arch_task_init(task, (size_t)entry, (size_t)stack_top);
}

void task_start(task_t *task) {
    // 挑选一个CPU
    uint64_t idle_mask = atomic_load(&g_idle_mask);
    uint64_t this_mask = 1ULL << cpu_index();
    if ((0 == idle_mask) || (idle_mask & this_mask)) {
        // 没有 idle cpu，或者当前 CPU 也 idle，则直接在当前 cpu 运行任务
        sched_cont(task, TS_STOPPED);
        // 自身 cpu，不执行 task_switch
        return;
    }

    // 在另一个 cpu 上运行任务
    int cpu = __builtin_ctzll(idle_mask);
    sched_cont_on(task, TS_STOPPED, cpu);
    arch_send_ipi(cpu, VEC_IPI_RESCHED); // 立即唤醒
}
