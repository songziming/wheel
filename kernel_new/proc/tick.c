#include "tick.h"
#include "sched.h"
#include <arch_intf.h>
#include <library/dllist.h>
#include <library/spin.h>
#include <library/debug.h>



static size_t g_tick = 0;
static dlnode_t g_tick_q;
static spin_t g_q_spin; // 控制队列的锁
static spin_t g_func_spin; // 执行回调函数的锁



// 注册一个计时器
void timer_start(timer_t *timer, int tick, timer_func_t func, void *arg1, void *arg2) {
    ASSERT(NULL != timer);
    ASSERT(NULL != func);

    timer->func = func;
    timer->arg1 = arg1;
    timer->arg2 = arg2;

    int key = irq_spin_take(&g_q_spin);
    ASSERT(!dl_contains(&g_tick_q, &timer->dl));

    dlnode_t *i = g_tick_q.next;
    for (; &g_tick_q != i; i = i->next) {
        timer_t *ref = containerof(i, timer_t, dl);
        if (ref->delta > tick) {
            ref->delta -= tick;
            break;
        }
        tick -= ref->delta;
    }
    timer->delta = tick;
    dl_insert_before(&timer->dl, i);

    irq_spin_give(&g_q_spin, key);
}

// 将 timer 从队列中移除时，被移除的 timer 也许正在另一个 CPU 运行
void timer_cancel(timer_t *timer) {
    int key = irq_spin_take(&g_q_spin);

    dlnode_t *next = dl_remove(&timer->dl);
    if (next) {
        ASSERT(dl_contains(&g_tick_q, next));
    }

    irq_spin_give(&g_q_spin, key);
}

// 保证在本函数返回后，timer 的回调函数未开始运行，或已结束运行
void timer_cancel_sync(timer_t *timer) {
    ASSERT(0 == cpu_int_depth()); // 不能在中断里调用

    int key = irq_spin_take(&g_q_spin);
    raw_spin_take(&g_func_spin);

    dlnode_t *next = dl_remove(&timer->dl);
    if (next) {
        ASSERT(dl_contains(&g_tick_q, next));
    }

    raw_spin_give(&g_func_spin);
    irq_spin_give(&g_q_spin, key);
}

static void timer_advance() {
    ASSERT(cpu_int_depth());

    int key = irq_spin_take(&g_q_spin);

    // 执行队列开头的 delta 为零的 timer
    while (!dl_is_lastone(&g_tick_q)) {
        timer_t *timer = containerof(g_tick_q.next, timer_t, dl);
        if (timer->delta > 0) {
            --timer->delta;
            break;
        }

        // 首先删除这个 timer，然后执行函数
        // 执行时钟函数时解除自旋锁，可以在函数里注册新的 timer
        dl_remove(&timer->dl);
        raw_spin_take(&g_func_spin);
        raw_spin_give(&g_q_spin);
        timer->func(timer->arg1, timer->arg2);
        raw_spin_give(&g_func_spin);

        // 锁住下一轮的队列
        raw_spin_take(&g_q_spin);
    }

    irq_spin_give(&g_q_spin, key);
}


INIT_TEXT void tick_init() {
    g_tick = 0;
    dl_init_circular(&g_tick_q);
    spin_init(&g_q_spin);
    spin_init(&g_func_spin);
}

inline size_t tick() {
    return g_tick;
}

// 由 arch 在每次时钟中断时调用
void tick_advance() {
    if (0 == cpu_index()) {
        ++g_tick;
        timer_advance();
    }

    // 时钟中断的最后执行调度
    sched_advance();
}
