#include "task.h"
#include <spin.h>
#include <debug.h>

// 任务管理 & 调度 & 定时任务

// static int g_tick = 0;



// 定时任务队列
static spin_t timer_lock;
static dlnode_t timer_q;


// 任务调度
PERCPU_BSS task_t *g_prev_task;
PERCPU_BSS task_t *g_next_task;

static INIT_BSS task_t g_dummy_task;

// 就绪任务队列
static PERCPU_BSS spin_t g_sched_lock;
static PERCPU_BSS int    g_queue_size;
static PERCPU_BSS task_t g_idle_task; // 这也是 ready-q 的头节点
static PERCPU_BSS uint8_t g_idle_stack[4096];

static atomic_int g_lowest_priority_cpu = 0;


//------------------------------------------------------------------------------
// 计时器
//------------------------------------------------------------------------------

INIT_TEXT void timer_init() {
    spin_init(&timer_lock);
    dl_init_circular(&timer_q);
}

// 挑出队列开头 delta==0 的节点，执行这些节点中的函数
void timer_process() {
    int key = irq_spin_take(&timer_lock);
    if (dl_is_lastone(&timer_q)) {
        irq_spin_give(&timer_lock, key);
        return;
    }

    // 找出队列开头所有 delta==0 的元素，停在第一个 delta!=0 的元素
    dlnode_t *newhead = timer_q.next;
    timerjob_t *job = containerof(newhead, timerjob_t, dl);
    job->delta--;
    while ((&timer_q != newhead) && (0 == job->delta)) {
        newhead = newhead->next;
        job = containerof(newhead, timerjob_t, dl);
    }

    // 将 delta==0 的子链表去掉
    dlnode_t *oldhead = timer_q.next;
    if (oldhead != newhead) {
        timer_q.next = newhead;
        newhead->prev = &timer_q;
    }

    irq_spin_give(&timer_lock, key);

    // 遍历被移除的子链表，运行里面的函数
    for (dlnode_t *dl = oldhead; dl != newhead; dl = dl->next) {
        timerjob_t *tmr = containerof(dl, timerjob_t, dl);
        tmr->func(tmr);
    }
}

void timer_start(timerjob_t *job, int tick) {
    int key = irq_spin_take(&timer_lock);
    ASSERT(!dl_contains(&timer_q, &job->dl));

    dlnode_t *dl;
    for (dl = timer_q.next; dl != &timer_q; dl = dl->next) {
        timerjob_t *ref = containerof(dl, timerjob_t, dl);
        if (tick < ref->delta) {
            ref->delta -= tick;
            break;
        }
        tick -= ref->delta;
    }

    job->delta = tick;
    dl_insert_before(&job->dl, dl);
    irq_spin_give(&timer_lock, key);
}

// 删除一个节点，需要把 delta 加到后一个节点之上
void timer_cancel(timerjob_t *job) {
    int key = irq_spin_take(&timer_lock);
    for (dlnode_t *dl = timer_q.next; dl != &timer_q; dl = dl->next) {
        if (&job->dl != dl) {
            continue;
        }
        if (dl->next != &timer_q) {
            timerjob_t *next = containerof(dl->next, timerjob_t, dl);
            next->delta += job->delta;
        }
        dl_remove(dl);
        break;
    }
    irq_spin_give(&timer_lock, key);
}



//------------------------------------------------------------------------------
// scheduler
//------------------------------------------------------------------------------


static NORETURN void idle_proc(task_t *self UNUSED) {
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

void sched_init() {
    g_dummy_task.name = "temp-dummy-TCB";

    spin_init(THISCPU(&g_sched_lock));
    THISCPU_SET(g_queue_size, 1);

    task_t *idle = THISCPU(&g_idle_task);
    // task_t *idle2 = NULL;
    // ASMV("leaq %%gs:%1,%0" : "=r"(idle2) : "m"(g_idle_task));
    // logk("idle=%p, idle2=%p\n", idle, idle2);

    uint8_t *top = THISCPU(g_idle_stack + sizeof(g_idle_stack));
    task_create(idle, "idle", idle_proc, top);
    dl_init_circular(&idle->dl);

    THISCPU_SET(g_prev_task, &g_dummy_task);
    THISCPU_SET(g_next_task, idle);
}

// run in ISR
// 在同一个优先级的任务之间轮转
void sched_process() {
    // spin_t *lock = THISCPU(&g_sched_lock);
    spin_t *lock = THISCPU(&g_sched_lock);
    raw_spin_take(lock);

    task_t *next = THISCPU_GET(g_next_task);
    next = containerof(next->dl.next, task_t, dl);
    if (THISCPU(&g_idle_task) == next) {
        next = containerof(next->dl.next, task_t, dl);
    }
    THISCPU_SET(g_next_task, next);
    raw_spin_give(lock);

    logk("*");
}

void sched_resume(task_t *task) {
    int cpu = atomic_load(&g_lowest_priority_cpu);
    logk("resuming task %s on cpu-%d\n", task->name, cpu);

    spin_t *lock = PERCPU(cpu, &g_sched_lock);
    task_t *idle = PERCPU(cpu, &g_idle_task);

    int key = irq_spin_take(lock);
    // TODO 不必放在队列最后，根据优先级找到合适的位置
    dl_insert_before(&task->dl, &idle->dl);
    irq_spin_give(lock, key);

    // WIP always run newly created task
    *PERCPU(cpu, &g_next_task) = task;
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

void task_create(task_t *task, const char *name, void *entry, void *stack_stop) {
    task->name = name;
    // task->entry = entry;
    arch_task_init(task, (size_t)entry, (size_t)stack_stop);
}
