#include "wdog.h"
#include <spinlock.h>
#include <debug.h>




// 看门狗（定时器）状态机
//
// 一个 wdog 的生命周期由 _Atomic state 字段管理，CAS 无锁协议决定 callback 的执行权。
// 状态可循环，wdog 可以重复使用。

//  WDOG_IDLE ←------------+----------------+
//      |                  |                |
//      | (wdog_start)     | (wdog_cancel)  |
//      ↓                  |                |
//  WDOG_ARMED ------------┘                | (wdog_cancel: spin-wait)
//      |                                   |
//      | (wdog_process)                    |
//      ↓                                   |
//  WDOG_FIRED -----------------------------+
//      |
//      | (callback done)
//      ↓
//  WDOG_IDLE

// 并发协议（关键保证）：
//   - wdog_process 从队列摘下 wdog 后，CAS(WDOG_ARMED, WDOG_FIRED) 决定是否执行 callback。
//   - wdog_cancel 从队列移除 wdog 后，CAS(WDOG_ARMED, WDOG_IDLE) 阻止 callback 执行。
//   - 如果 wdog_cancel 发现 wdog 已被摘下（不在队列中），说明 wdog_process 正在处理：
//        CAS(WDOG_ARMED, WDOG_IDLE) 成功 → 抢在 callback 前取消，callback 不会执行。
//        CAS 失败（state==WDOG_FIRED）→ callback 正在执行，自旋等待其完成（state 变为 WDOG_IDLE）。
//   - wdog_cancel 返回 ⇒ callback 不会（再）被调用。
//     这个保证使动态生命周期的对象（kobj、sema 等）可以安全释放。


// 定时任务队列
static spinlock_t timer_lock = SPINLOCK_INIT;
static DEFINE_DL_HEAD(timer_q);


// INIT_TEXT void wdog_init() {
//     dl_init_circular(&timer_q);
// }

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

        int expected = WDOG_ARMED;
        if (atomic_compare_exchange_strong(&old->state, &expected, WDOG_FIRED)) {
            old->func(old);     // WDOG_ARMED --> WDOG_FIRED
            old->state = WDOG_IDLE;  // WDOG_FIRED --> WDOG_IDLE
        }
    }
}

void wdog_start(wdog_t *wd, wdog_cb_t func, int tick) {
    SPINLOCK_SCOPED(&timer_lock);
    ASSERT(!dl_contains(&timer_q, &wd->dl));
    // ASSERT(wd->state == WDOG_IDLE);

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
    wd->state = WDOG_ARMED;
    dl_insert_before(&wd->dl, dl);
}

// 删除一个节点，需要把 delta 加到后一个节点之上
// 关键保证：返回后 callback 不会（再）被调用
//   - 若 wd 仍为 WDOG_ARMED：CAS 置 WDOG_IDLE，从队列移除，callback 永不执行
//   - 若 wd 已被 wdog_process 摘下（不在队列）：
//       CAS(WDOG_ARMED,WDOG_IDLE) 失败，说明 callback 正在执行或已执行完
//       此时 wd 不在 timer_q 中，无需操作链表，放掉 timer_lock 再自旋等回调结束
void wdog_cancel(wdog_t *wd) {
    {
        SPINLOCK_SCOPED(&timer_lock);
        int expected = WDOG_ARMED;
        if (atomic_compare_exchange_strong(&wd->state, &expected, WDOG_IDLE)) {
            // WDOG_ARMED->WDOG_IDLE，callback 之后也不会执行
            if (wd->dl.prev) {
                dlnode_t *rear = dl_remove(&wd->dl);
                if (rear) {
                    wdog_t *next = containerof(rear, wdog_t, dl);
                    next->delta += wd->delta;
                }
            }
            return;
        }
    }

    // CAS 失败：wd 已不在 timer_q，state 只可能是 WDOG_FIRED(回调进行中) 或 WDOG_IDLE(回调已结束)
    // 不持 timer_lock 自旋，避免阻塞 CPU0 的 wdog_process
    while (atomic_load(&wd->state) == WDOG_FIRED) {
        cpu_pause();
    }
}
