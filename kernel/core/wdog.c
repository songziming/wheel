#include "wdog.h"
#include <spinlock.h>
#include <debug.h>




// 看门狗（定时器）状态机
//
// 一个 wdog 的生命周期由 _Atomic state 字段管理，CAS 无锁协议决定 callback 的执行权。
// 状态可循环，wdog 可以重复使用。
//
//   IDLE ──[wdog_start]──→ ARMED ──[wdog_process]──→ FIRED ──[callback done]──→ IDLE
//     ↑                      │                         │
//     └────[wdog_cancel]─────┘                         │
//     ↑                                                │
//     └────────────[wdog_cancel: spin-wait]────────────┘
//
// 并发协议（关键保证）：
//   - wdog_process 从队列摘下 wdog 后，CAS(ARMED, FIRED) 决定谁执行 callback。
//   - wdog_cancel 从队列移除 wdog 后，CAS(ARMED, IDLE) 阻止 callback 执行。
//   - 如果 wdog_cancel 发现 wdog 已被摘下（不在队列中），说明 wdog_process 正在处理：
//        CAS(ARMED, IDLE) 成功 → 抢在 callback 前取消，callback 不会执行。
//        CAS 失败（state==FIRED）→ callback 正在执行，自旋等待其完成（state 变为 IDLE）。
//   - wdog_cancel 返回 ⇒ callback 不会（再）被调用。
//     这个保证使动态生命周期的对象（kobj、sema 等）可以安全释放。


enum wdog_state {
    IDLE  = 0,  // 刚被创建，或已经取消，或 timeout ISR 已执行结束
    ARMED = 1,  // 已放入全局定时器队列
    FIRED = 2,  // 已超时，开始执行 timeout ISR 函数
};


// 定时任务队列
static spinlock_t timer_lock = SPINLOCK_INIT;
static dlnode_t timer_q;


INIT_TEXT void wdog_init() {
    dl_init_circular(&timer_q);
}

// 挑出队列开头 delta==0 的节点，执行这些节点中的函数
void wdog_process() {
    dlnode_t *oldhead = NULL;
    dlnode_t *newhead = NULL;

    {
        SPINLOCK_SCOPED(&timer_lock);
        if (dl_is_lastone(&timer_q)) {
            return;
        }

        // 找出队列开头所有 delta==0 的元素，摘出链表，并设置 prev=NULL
        // 停在第一个 delta!=0 的元素
        newhead = timer_q.next;
        wdog_t *wd = containerof(newhead, wdog_t, dl);
        while ((&timer_q != newhead) && (wd->delta <= 0)) {
            newhead->prev = NULL;
            newhead = newhead->next;
            wd = containerof(newhead, wdog_t, dl);
        }

        // 已经停在第一个 delta!=0 的元素
        if (&timer_q != newhead) {
            ASSERT(wd->delta > 0);
            wd->delta--;
        }

        // 将链表开头 delta==0 的子序列去掉
        // 被摘掉的元素构成单链表
        oldhead = timer_q.next;
        if (oldhead != newhead) {
            timer_q.next = newhead;
            newhead->prev = &timer_q;
        }
    }

    // 遍历被移除的子链表，运行里面的函数
    while (oldhead != newhead) {
        wdog_t *old = containerof(oldhead, wdog_t, dl);
        oldhead = oldhead->next;

        int expected = ARMED;
        if (atomic_compare_exchange_strong(&old->state, &expected, FIRED)) {
            old->func(old);     // ARMED --> FIRED
            old->state = IDLE;  // FIRED --> IDLE
        }
    }
}

void wdog_start(wdog_t *wd, wdog_cb_t func, int tick) {
    SPINLOCK_SCOPED(&timer_lock);
    ASSERT(!dl_contains(&timer_q, &wd->dl));
    ASSERT(wd->state == IDLE);

    dlnode_t *dl;
    for (dl = timer_q.next; dl != &timer_q; dl = dl->next) {
        wdog_t *ref = containerof(dl, wdog_t, dl);
        if (tick < ref->delta) {
            ref->delta -= tick;
            break;
        }
        tick -= ref->delta;
    }

    wd->delta = tick;
    wd->func = func;
    wd->state = ARMED;
    dl_insert_before(&wd->dl, dl);
}

// 删除一个节点，需要把 delta 加到后一个节点之上
void wdog_cancel(wdog_t *wd) {
    SPINLOCK_SCOPED(&timer_lock);

    int expected = ARMED;
    if (!atomic_compare_exchange_strong(&wd->state, &expected, IDLE)) {
        // 如果状态不是 ARMED，说明 wd 不在队列中
        // 有可能这个 wdog 已经触发，需等待 timeout callback 执行结束
        while (atomic_load(&wd->state) == FIRED) {
            cpu_pause();
        }
        return;
    }

    // ARMED->IDLE，callback 之后也不会执行
    // 检查 dl-prev 可以判断是否还在链表中，可能已经被 wdog_process 移出链表
    if (wd->dl.prev) {
        dlnode_t *rear = dl_remove(&wd->dl);
        if (rear) {
            wdog_t *next = containerof(rear, wdog_t, dl);
            next->delta += wd->delta;
        }
    }
}
