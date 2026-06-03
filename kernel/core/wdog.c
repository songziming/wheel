#include "wdog.h"
#include <spin.h>
#include <debug.h>


// 定时任务队列
static spin_t timer_lock = SPIN_INIT;
static dlnode_t timer_q;


INIT_TEXT void wdog_init() {
    dl_init_circular(&timer_q);
}

// 挑出队列开头 delta==0 的节点，执行这些节点中的函数
void wdog_process() {
    int key = irq_spin_take(&timer_lock);
    if (dl_is_lastone(&timer_q)) {
        irq_spin_give(&timer_lock, key);
        return;
    }

    // 找出队列开头所有 delta==0 的元素，停在第一个 delta!=0 的元素
    dlnode_t *newhead = timer_q.next;
    wdog_t *tmr = containerof(newhead, wdog_t, dl);
    while ((&timer_q != newhead) && (tmr->delta <= 0)) {
        newhead = newhead->next;
        tmr = containerof(newhead, wdog_t, dl);
    }

    // 停在第一个 delta!=0 的元素
    if (&timer_q != newhead) {
        ASSERT(tmr->delta > 0);
        tmr->delta--;
    }

    // 将链表开头 delta==0 的子序列去掉
    dlnode_t *oldhead = timer_q.next;
    if (oldhead != newhead) {
        timer_q.next = newhead;
        newhead->prev = &timer_q;
    }

    irq_spin_give(&timer_lock, key);

    // 遍历被移除的子链表，运行里面的函数
    while (oldhead != newhead) {
        wdog_t *old = containerof(oldhead, wdog_t, dl);
        oldhead = oldhead->next;
        old->func(old);
    }
}

void wdog_start(wdog_t *tmr, wdog_cb_t func, int tick) {
    int key = irq_spin_take(&timer_lock);
    ASSERT(!dl_contains(&timer_q, &tmr->dl));

    dlnode_t *dl;
    for (dl = timer_q.next; dl != &timer_q; dl = dl->next) {
        wdog_t *ref = containerof(dl, wdog_t, dl);
        if (tick < ref->delta) {
            ref->delta -= tick;
            break;
        }
        tick -= ref->delta;
    }

    tmr->delta = tick;
    tmr->func = func;
    dl_insert_before(&tmr->dl, dl);
    irq_spin_give(&timer_lock, key);
}

// 删除一个节点，需要把 delta 加到后一个节点之上
void wdog_cancel(wdog_t *tmr) {
    int key = irq_spin_take(&timer_lock);
    for (dlnode_t *dl = timer_q.next; dl != &timer_q; dl = dl->next) {
        if (&tmr->dl != dl) {
            continue;
        }
        if (dl->next != &timer_q) {
            wdog_t *next = containerof(dl->next, wdog_t, dl);
            next->delta += tmr->delta;
        }
        dl_remove(dl);
        break;
    }
    irq_spin_give(&timer_lock, key);
}
