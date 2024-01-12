#include <tick.h>
#include <wheel.h>


// 有些工作只能在中断返回阶段执行，例如删除当前任务
// 也可以注册某些工作在若干 tick 之后运行

// TODO 还是应该把 work、watchdog 区分开
//      work_q 每次中断都执行（中断退出过程），直接清空队列
//      watchdog 只有时钟中断才执行，与系统 tick 一致

// defered work 分为两类：一类在中断里执行、一类在任务里执行
// 启动一个 work-daemon task，专门负责执行到时间的 work


// 系统全局时间戳
static size_t g_tick = 0;


// 全局只有一个定时任务队列
// 按照剩余时间从小到大排序
static spin_t g_work_spin;
static dlnode_t g_work_q;


INIT_TEXT void work_init() {
    g_work_spin = SPIN_INIT;
    dl_init_circular(&g_work_q);
    // for (int i = 0; i < cpu_count(); i++) {
    //     dlnode_t *q = pcpu_ptr(i, &g_work_q);
    //     dl_init_circular(q);
    // }
}


// 安排一个工作，若干 tick 后执行
// 如果 tick==0，则下一次中断返回时立即执行
// caller 需要保证持有 work 的自旋锁
void work_delay(work_t *work, int tick, work_func_t func, void *arg) {
    ASSERT(NULL != work);
    ASSERT(NULL != func);
    ASSERT(!dl_is_wired(&work->node));

    work->tick = tick;
    work->func = func;
    work->arg = arg;

    int key = irq_spin_take(&g_work_spin);

    // 在链表中寻找第一个剩余时间大于 tick 的工作
    // dlnode_t *q = this_ptr(&g_work_q);
    dlnode_t *i = g_work_q.next;
    for (; &g_work_q != i; i = i->next) {
        work_t *w = containerof(i, work_t, node);
        if (w->tick > tick) {
            break;
        }
    }
    dl_insert_before(&work->node, i);

    irq_spin_give(&g_work_spin, key);
}


void work_cancel(work_t *work) {
    ASSERT(NULL != work);

    // TODO 这个 work 可能注册在另一个 CPU 的工作队列中，而不是当前 CPU
    if (dl_is_wired(&work->node)) {
        dl_remove(&work->node);
    }
}



size_t tick_get() {
    return g_tick;
}



// 执行队列中的所有工作，处于中断上下文
static void work_flush() {
    dlnode_t *q = this_ptr(&g_work_q);

    while (!dl_is_lastone(q)) {
        work_t *w = containerof(q->next, work_t, node);
        if (0 != w->tick) {
            break;
        }

        // 首先从队列中删除，再运行定时函数
        // 函数内部可能要删除 work 结构体
        // TODO 执行之前，将 work 标记为已完成
        dl_remove(&w->node);
        w->node.prev = NULL;
        w->func(w->arg);
    }

    for (dlnode_t *i = q->next; i != q; i = i->next) {
        work_t *w = containerof(i, work_t, node);
        --w->tick;
    }
}


// 每次时钟中断执行，每个 CPU 都执行
void tick_advance() {
    if (0 == cpu_index()) {
        ++g_tick;
    }
    work_flush();
    sched_tick();
}
