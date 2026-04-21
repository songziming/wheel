#include "task.h"
#include <spin.h>
#include <kstring.h>
#include <debug.h>

// 任务管理 & 调度 & 定时任务

// static int g_tick = 0;


// 任务调度
PERCPU_BSS task_t *g_prev_task;
PERCPU_BSS task_t *g_next_task;
static INIT_BSS task_t g_dummy_task;
static PERCPU_BSS task_t g_idle_task; // 这也是 ready-q 的头节点
static PERCPU_BSS uint8_t g_idle_stack[1024]; // TODO percpu 负责划分


// 就绪任务队列
static PERCPU_BSS spin_t g_sched_lock;
static PERCPU_BSS int    g_queue_size;
static PERCPU_BSS rdyq_t g_rdy_queue;

static atomic_int g_lowest_priority_cpu = 0;




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
        q->heads[prio] = NULL;
        q->priorities &= ~(1U << prio);
    } else {
        if (dl == q->heads[prio]) {
            q->heads[prio] = dl->next;
        }
        dl_remove(dl);
    }
}

dlnode_t *rdyq_head(rdyq_t *q) {
    int prio = __builtin_ctz(q->priorities);
    return q->heads[prio];
}

dlnode_t *rdyq_rotate(rdyq_t *q UNUSED, dlnode_t *dl) {
    return dl->next;
}


//------------------------------------------------------------------------------
// scheduler
//------------------------------------------------------------------------------

// 任务状态切换
// 管理就绪队列

static NORETURN void idle_proc(task_t *self UNUSED) {
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

void sched_init() {
    g_dummy_task.name = "temp-dummy-TCB";
    // g_dummy_task.priority = 31;

    spin_init(THISCPU(&g_sched_lock));
    THISCPU_SET(g_queue_size, 1);

    task_t *idle = THISCPU(&g_idle_task);
    uint8_t *top = THISCPU(g_idle_stack + sizeof(g_idle_stack));
    task_create_ex(idle, "idle", 31, (size_t)top, idle_proc);
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
    task = containerof(task->dl.next, task_t, dl);
    THISCPU_SET(g_next_task, task);

    // raw_spin_give(lock);
    logk("*");
}

void sched_stop(task_t *task, task_state_t bits) {
    if (TS_READY == task->state) {
        // TODO remove from ready-queue
    }

    task->state |= bits;
}

void sched_resume_at(task_t *task, int cpu) {
    spin_t *lock = PERCPU(cpu, &g_sched_lock);
    rdyq_t *q = PERCPU(cpu, &g_rdy_queue);

    int key = irq_spin_take(lock);
    rdyq_insert(q, &task->dl, task->priority);
    irq_spin_give(lock, key);

    // 判断能否抢占
    task_t *next = *PERCPU(cpu, &g_next_task);
    if (task->priority < next->priority) {
        *PERCPU(cpu, &g_next_task) = task;
    }
}

void sched_resume(task_t *task) {
    int cpu = atomic_load(&g_lowest_priority_cpu);
    sched_resume_at(task, cpu);
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
    task->stack.desc = name;
    size_t stack_va = vmspace_alloc_stack(&g_kernel_vm, &task->stack, 0);
    size_t stack_top = stack_va + PAGE_SIZE;
    task_create_ex(task, name, priority, stack_top, entry);
}
