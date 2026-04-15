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
static PERCPU_BSS task_t g_idle_task;


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

void sched_init() {
    g_dummy_task.name = "temp-dummy-TCB";
    THISCPU_SET(g_prev_task, &g_dummy_task);

    // task_t *idle = THISCPU(&g_idle_task);
    // TODO create task IDLE, put on this ready-queue
}

void sched_process() {
    task_t *prev = THISCPU_GET(g_prev_task);
    task_t *next = THISCPU_GET(g_next_task);
    if (prev != next) {
        logk("\n keeping old task %s\n", prev->name);
        THISCPU_SET(g_next_task, prev);
    }
    logk("*");
}
