#include "task.h"
#include <arch_api.h>
#include <spin.h>
#include <debug.h>

// 任务管理 & 调度 & 定时任务

static int g_tick = 0;



// 定时任务队列
static spin_t timer_lock;
static dlnode_t timer_q;


INIT_TEXT void timer_init() {
    spin_init(&timer_lock);
    dl_init_circular(&timer_q);
}

// 挑出队列开头 delta==0 的节点，执行这些节点中的函数
void timer_forward() {
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
        if (newhead) {
            newhead->prev = &timer_q;
        } else {
            timer_q.prev = NULL;
        }
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



void tick_advance() {
    if (0 == cpu_index()) {
        ++g_tick;
    }
}
