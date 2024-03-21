// 定时器，在若干时间后运行函数

#include <timer.h>
#include <wheel.h>


static dlnode_t g_timer_q;
static spin_t g_timer_spin = SPIN_INIT;



INIT_TEXT void timer_lib_init() {
    dl_init_circular(&g_timer_q);
}


// 在时钟中断里调用
void timer_proceed() {
    ASSERT(cpu_int_depth());

    int key = irq_spin_take(&g_timer_spin);

    // 执行队列开头的 delta 为零的 timer
    while (!dl_is_lastone(&g_timer_q)) {
        timer_t *timer = containerof(g_timer_q.next, timer_t, dl);
        if (timer->delta > 0) {
            --timer->delta;
            break;
        }

        // 首先删除这个 timer，然后执行函数
        // 执行时钟函数时解除自旋锁，可以在函数里注册新的 timer
        dl_remove(&timer->dl);
        irq_spin_give(&g_timer_spin, key);
        timer->func(timer->arg1, timer->arg2);
        key = irq_spin_take(&g_timer_spin);
    }

    irq_spin_give(&g_timer_spin, key);
}



// 设置一个 singleshot 计时器
// 如果传入的 tick==0，则下一次时钟中断就执行，如果当前就在时钟中断里，则本次执行
void timer_start(timer_t *timer, int tick, timer_func_t func, void *a1, void *a2) {
    ASSERT(NULL != timer);
    ASSERT(NULL != func);

    timer->func = func;
    timer->arg1 = a1;
    timer->arg2 = a2;

    int key = irq_spin_take(&g_timer_spin);
    ASSERT(!dl_contains(&g_timer_q, &timer->dl));

    dlnode_t *i = g_timer_q.next;
    for (; &g_timer_q != i; i = i->next) {
        timer_t *ref = containerof(i, timer_t, dl);
        if (ref->delta > tick) {
            ref->delta -= tick;
            break;
        }
        tick -= ref->delta;
    }
    timer->delta = tick;
    dl_insert_before(&timer->dl, i);

    irq_spin_give(&g_timer_spin, key);
}

// NOTE 由于 timer_proceed 首先删除 timer，之后才执行函数
//      导致 cancel 返回时，被删除的 timer 可能正在运行
void timer_cancel(timer_t *timer) {
    int key = irq_spin_take(&g_timer_spin);

    dlnode_t *next = dl_remove(&timer->dl);
    if (next) {
        ASSERT(dl_contains(&g_timer_q, next));
    }

    irq_spin_give(&g_timer_spin, key);
}
