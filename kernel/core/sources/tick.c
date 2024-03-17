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
static size_t g_tick_count = 0;


// 全局只有一个定时任务队列，在 CPU0 运行（不同 CPU 的时钟中断并非同时发生）
// 按照剩余时间从小到大排序
static spin_t g_tick_spin = SPIN_INIT;
static dlnode_t g_tick_q;


// 每个 CPU 都有自己的延迟调用队列
// TODO 这个队列放在专门的 work.c 文件中
static PCPU_DATA spin_t g_work_spin = SPIN_INIT;
static PCPU_BSS dlnode_t g_work_q;



//------------------------------------------------------------------------------
// 定时函数调用，在若干 tick 后的中断里执行（CPU-0）
//------------------------------------------------------------------------------

INIT_TEXT void tick_init() {
    dl_init_circular(&g_tick_q);
}

size_t tick_get() {
    return g_tick_count;
}

// caller 需要保证持有 work 的自旋锁
void tick_delay(work_t *work, int tick, work_func_t func, void *arg1, void *arg2) {
    ASSERT(NULL != work);
    ASSERT(NULL != func);
    ASSERT(!dl_contains(&g_tick_q, &work->node));

    work->tick = tick;
    work->func = func;
    work->arg1 = arg1;
    work->arg2 = arg2;

    int key = irq_spin_take(&g_tick_spin);

    // 在链表中寻找第一个剩余时间大于 tick 的工作
    dlnode_t *i = g_tick_q.next;
    for (; &g_tick_q != i; i = i->next) {
        work_t *w = containerof(i, work_t, node);
        if (w->tick > tick) {
            break;
        }
    }
    dl_insert_before(&work->node, i);

    irq_spin_give(&g_tick_spin, key);
}

// 取消一个任务
void work_cancel(work_t *work) {
    ASSERT(NULL != work);

    int key = irq_spin_take(&g_tick_spin);
    ASSERT(dl_contains(&g_tick_q, &work->node));
    dl_remove(&work->node);
    irq_spin_give(&g_tick_spin, key);
}

// 每次时钟中断执行，每个 CPU 都执行
void tick_advance() {
    sched_tick();

    if (0 != cpu_index()) {
        return;
    }

    // 只有 CPU-0 需要处理计时
    ++g_tick_count;

    // 执行计时函数
    // watchdog 执行过程中，可能又注册了新的定时函数，tick_q 可能发生表更
    // 因此不断读取 tick_q 的头节点，而不是遍历链表

    int key = irq_spin_take(&g_tick_spin);

    while (!dl_is_lastone(&g_tick_q)) {
        work_t *w = containerof(g_tick_q.next, work_t, node);
        if (0 != w->tick) {
            break;
        }

        // 首先从队列中删除，再运行定时函数，函数内部可能要删除 work 所在结构体
        dl_remove(&w->node);

        // 这个 work 函数内部可能注册新的 work，需要此处释放锁
        irq_spin_give(&g_tick_spin, key);
        w->func(w->arg1, w->arg2);
        key = irq_spin_take(&g_tick_spin);
    }

    for (dlnode_t *i = g_tick_q.next; i != &g_tick_q; i = i->next) {
        work_t *w = containerof(i, work_t, node);
        --w->tick;
    }

    irq_spin_give(&g_tick_spin, key);
}


//------------------------------------------------------------------------------
// 延迟调用，在下一次中断返回过程中执行
//------------------------------------------------------------------------------

INIT_TEXT void work_init() {
    for (int i = 0; i < cpu_count(); i++) {
        dlnode_t *q = pcpu_ptr(i, &g_work_q);
        dl_init_circular(q);
    }
}

void work_defer(work_t *work, work_func_t func, void *arg1, void *arg2) {
    ASSERT(NULL != work);
    ASSERT(NULL != func);

    work->func = func;
    work->arg1 = arg1;
    work->arg2 = arg2;

    spin_t *lock = this_ptr(&g_work_spin);
    dlnode_t *q = this_ptr(&g_work_q);

    int key = irq_spin_take(lock);
    dl_insert_before(&work->node, q);
    irq_spin_give(lock, key);
}


// 在中断返回阶段执行，执行所有的函数
void work_q_flush() {
    spin_t *lock = this_ptr(&g_work_spin);
    dlnode_t *q = this_ptr(&g_work_q);

    int key = irq_spin_take(lock);

    if (dl_is_lastone(q)) {
        irq_spin_give(lock, key);
        return;
    }

    // work 函数在执行中可能又注册了新的 work，即 work_q 在迭代过程中更新了
    // 这里先把头节点取出，改造为双向不循环链表，原本的头节点形成一个新的链表
    // 这样就可以遍历原来的链表，还可以向新的链表注册延迟调用任务
    dlnode_t *head = q->next;
    dlnode_t *tail = q->prev;
    ASSERT(head != q);
    ASSERT(tail != q);
    tail->next = NULL;
    dl_init_circular(q);
    irq_spin_give(lock, key);

    // 执行 work 函数时临时释放锁，否则无法注册新的 work
    while (head) {
        work_t *w = containerof(head, work_t, node);
        head = head->next; // 先访问下一个 node，然后再执行 work
        w->func(w->arg1, w->arg2); // work 执行中可能会删除 work_t 对象，导致无法访问 next 字段
    }
}
