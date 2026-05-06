#include "task.h"
#include <spin.h>
#include <work.h>
#include <kstring.h>
#include <debug.h>

// 任务管理控制任务的生命周期，状态切换
// 调度管理 ready 状态的任务


static INIT_DATA task_t g_dummy_task = { .name = "dummy-TCB" };
static PERCPU_BSS task_t g_idle_task;

// 就绪任务队列
static PERCPU_BSS rdyq_t g_rdy_queue; // ready-queue
PERCPU_DATA task_t *g_prev_task = NULL;
PERCPU_BSS task_t *g_next_task; // also guarded by rdyq->lock

// 标记空载状态，新任务优先放在空载 CPU 上
static _Atomic uint64_t g_idle_mask;

// 每个 CPU 就绪队列中非 idle 任务的数量，在 rdyq lock 内修改
static PERCPU_BSS int g_rdy_count;

// 收到 IPI_MIGRATE 的 CPU 应将一个任务迁移到此目标 CPU，-1 表示没有请求
static PERCPU_BSS int g_migrate_target;

// 抢占禁用深度，> 0 时中断返回不切换任务
PERCPU_DATA int g_preempt_depth = 0;

// 僵尸任务链表，等待回收栈和 TCB
static PERCPU_DATA spin_t g_zombie_lock = SPIN_INIT;
static PERCPU_BSS dlnode_t g_zombie_list;
static PERCPU_BSS work_t g_reap_work;

static void sched_request_migrate();
static void reap_work_func(work_t *wk UNUSED);




//------------------------------------------------------------------------------
// ready queue
//------------------------------------------------------------------------------

// 就绪队列只关心队列，不管自旋锁

INIT_TEXT void rdyq_init(rdyq_t *q) {
    kmemset(q, 0, sizeof(*q));
    q->lock = SPIN_INIT;
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
    idle->affinity = cpu_index();
    rdyq_insert(q, &idle->dl, 31);

    // if (0 == cpu_index()) {
    //     dl_init_circular(&g_zombie_list);
    // }
    dl_init_circular(THISCPU(&g_zombie_list));

    atomic_fetch_or(&g_idle_mask, 1U << cpu_index());
    THISCPU_SET(g_prev_task, &g_dummy_task);
    THISCPU_SET(g_next_task, idle);
    THISCPU_SET(g_migrate_target, -1);
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
            atomic_fetch_or(&g_idle_mask, 1U << cpu_index());
        }
    }

    THISCPU_SET(g_next_task, task);
    irq_spin_give(&q->lock, key);

    if (31 == task->priority) {
        sched_request_migrate();
    }
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
    (*THISCPU(&g_rdy_count))--;

    dlnode_t *head = rdyq_head(q);
    task_t *next = containerof(head, task_t, dl);
    if (THISCPU(&g_idle_task) == next) {
        atomic_fetch_or(&g_idle_mask, 1U << cpu_index());
    }

    THISCPU_SET(g_next_task, next);
    irq_spin_give(&q->lock, key);

    return self;
}

//------------------------------------------------------------------------------
// migration
//------------------------------------------------------------------------------

// 挑选负载最高的 CPU（就绪任务数量最多），发送迁移请求
// 在 sched_process() 选到 idle 后调用，不持有 rdyq lock
static void sched_request_migrate() {
    int me = cpu_index();
    int victim = -1;
    int max_count = 0;

    for (int i = 0; i < cpu_count(); i++) {
        if (i == me) continue;
        int cnt = *PERCPU(i, &g_rdy_count);
        if (cnt > max_count) {
            max_count = cnt;
            victim = i;
        }
    }

    if (victim < 0) return;

    *PERCPU(victim, &g_migrate_target) = me;
    arch_send_ipi(victim, VEC_IPI_MIGRATE);
}

// 响应 VEC_IPI_MIGRATE：从自己的就绪队列中选一个任务捐给请求方
// 在请求方 CPU 的 ISR 上下文中执行
void sched_try_migrate() {
    int me = cpu_index();
    int target = *THISCPU(&g_migrate_target);
    *THISCPU(&g_migrate_target) = -1;

    if (target < 0 || target >= cpu_count() || target == me) return;

    rdyq_t *myq = THISCPU(&g_rdy_queue);
    int key = irq_spin_take(&myq->lock);

    task_t *prev = THISCPU_GET(g_prev_task);
    task_t *dntd = NULL;  // donated

    // 从低优先级找可迁移任务：无 affinity、非 g_prev_task、非 idle
    for (int prio = 30; prio >= 0; prio--) {
        if (NULL == myq->heads[prio]) continue;
        dlnode_t *dl = myq->heads[prio];
        dlnode_t *start = dl;
        do {
            task_t *t = containerof(dl, task_t, dl);
            if (t != prev && t->affinity < 0) {
                dntd = t;
                break;
            }
            dl = dl->next;
        } while (dl != start);
        if (dntd) {
            rdyq_remove(myq, &dntd->dl, prio);
            break;
        }
    }

    if (dntd) {
        (*THISCPU(&g_rdy_count))--;
        if (dntd == THISCPU_GET(g_next_task)) {
            dlnode_t *head = rdyq_head(myq);
            THISCPU_SET(g_next_task, containerof(head, task_t, dl));
        }
    }

    irq_spin_give(&myq->lock, key);

    if (NULL == dntd) return;

    // 插入目标 CPU 的就绪队列
    rdyq_t *tq = PERCPU(target, &g_rdy_queue);
    key = irq_spin_take(&tq->lock);

    rdyq_insert(tq, &dntd->dl, dntd->priority);
    (*PERCPU(target, &g_rdy_count))++;

    task_t *tgt_idle = PERCPU(target, &g_idle_task);
    if (*PERCPU(target, &g_next_task) == tgt_idle) {
        atomic_fetch_and(&g_idle_mask, ~(1ULL << target));
    }
    *PERCPU(target, &g_next_task) = containerof(rdyq_head(tq), task_t, dl);

    irq_spin_give(&tq->lock, key);

    arch_send_ipi(target, VEC_IPI_RESCHED);
}

// 在当前 CPU 恢复运行这个 task
// 但不要立即触发 task-switch
void sched_cont(task_t *task, uint32_t bits) {
    ASSERT(TS_READY != task->state);
    ASSERT(task->affinity < 0 || task->affinity == cpu_index());

    task->state &= ~bits;
    if (TS_READY != task->state) {
        return; // still no ready
    }
    task->tick = task->tick_reload;

    rdyq_t *q = THISCPU(&g_rdy_queue);
    int key = irq_spin_take(&q->lock);

    rdyq_insert(q, &task->dl, task->priority);
    (*THISCPU(&g_rdy_count))++;
    task_t *next = containerof(rdyq_head(q), task_t, dl);
    if (THISCPU_GET(g_next_task) == THISCPU(&g_idle_task)) {
        atomic_fetch_and(&g_idle_mask, ~(1U << cpu_index()));
    }

    THISCPU_SET(g_next_task, next);
    irq_spin_give(&q->lock, key);
}


// 在另一个 cpu 上启动运行任务
void sched_cont_on(task_t *task, uint32_t bits, int cpu) {
    ASSERT(TS_READY != task->state);
    ASSERT(task->affinity < 0 || task->affinity == cpu);
    if (cpu_index() == cpu) {
        sched_cont(task, bits);
        return;
    }

    task->state &= ~bits;
    if (TS_READY != task->state) {
        return; // still no ready
    }
    task->tick = task->tick_reload;

    rdyq_t *q = PERCPU(cpu, &g_rdy_queue);
    int key = irq_spin_take(&q->lock);

    rdyq_insert(q, &task->dl, task->priority);
    (*PERCPU(cpu, &g_rdy_count))++;
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

void preempt_disable() {
    THISCPU_SET(g_preempt_depth, THISCPU_GET(g_preempt_depth) + 1);
}

void preempt_enable() {
    THISCPU_SET(g_preempt_depth, THISCPU_GET(g_preempt_depth) - 1);
}

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
    task->affinity = -1;
    arch_task_init(task, (size_t)entry, (size_t)stack_top);
}

void task_start(task_t *task) {
    // int cpu = task->affinity;
    // if (cpu < 0) {
    //     uint64_t idle_mask = atomic_load(&g_idle_mask);
    //     uint64_t this_mask = 1ULL << cpu_index();
    // }
    if (task->affinity >= 0) {
        if (task->affinity == cpu_index()) {
            sched_cont(task, TS_STOPPED);
        } else {
            // logk("2 starting task %s on %d\n", task->name, task->affinity);
            sched_cont_on(task, TS_STOPPED, task->affinity);
            arch_send_ipi(task->affinity, VEC_IPI_RESCHED);
        }
        return;
    }

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
    ASSERT(cpu_index() != cpu);
    // logk("3 starting task %s on %d\n", task->name, cpu);
    sched_cont_on(task, TS_STOPPED, cpu);
    arch_send_ipi(cpu, VEC_IPI_RESCHED); // 立即唤醒
}

//------------------------------------------------------------------------------
// task exit & cleanup
//------------------------------------------------------------------------------

// 回收已退出任务的资源，必须在中断栈或非当前任务的上下文中调用
static void task_free(task_t *task) {
    ASSERT(task->state != TS_READY);
    vmspace_remove(&g_kernel_vm, &task->stack);

    // TODO: 回收 TCB（当 TCB 动态分配时）
    // task_t 目前是静态分配的，以后若改用 slab 或动态分配，
    // 在此处将 task 归还给分配器。
}

// work 回调：收割僵尸链表上所有等待回收的任务
// 运行在中断栈上，因此可以安全地释放 task 的栈
static void reap_work_func(work_t *wk UNUSED) {
    spin_t *lock = THISCPU(&g_zombie_lock);
    dlnode_t *list = THISCPU(&g_zombie_list);

    int key = irq_spin_take(lock);
    dlnode_t *dl = list->next;
    dl_init_circular(list);

    for (; dl != list; dl = dl->next) {
        task_free(containerof(dl, task_t, dl));
    }
    irq_spin_give(lock, key);
}

// 退出当前任务。将自身放入僵尸链表，注册 work 在下一次中断返回
// 流程中回收资源（此时已在中断栈上），然后切换走，永不返回。
NORETURN void task_exit() {
    task_t *self = sched_stop_self(TS_STOPPED);
    logk("delete self task %s\n", self->name);

    spin_t *lock = THISCPU(&g_zombie_lock);
    dlnode_t *list = THISCPU(&g_zombie_list);

    int key = irq_spin_take(lock);
    dl_insert_before(&self->dl, list);
    irq_spin_give(lock, key);

    // work 在 arch_task_switch → int_return_to_task → work_flush 中执行
    // 此时已离开当前栈，运行在中断栈上，可以安全释放栈内存
    work_defer(THISCPU(&g_reap_work), reap_work_func, "delete task");

    arch_task_switch();

    while (1) {
        cpu_halt();
    }
}
