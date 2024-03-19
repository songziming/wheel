#include <tick.h>
#include <wheel.h>


static size_t g_tick_count = 0;


size_t tick_get() {
    return g_tick_count;
}

void tick_advance() {
    sched_tick();

    if (0 != cpu_index()) {
        return;
    }

    // 只有 CPU-0 需要处理计时、执行时钟函数
    ++g_tick_count;
    timer_proceed();
}


//------------------------------------------------------------------------------
// 延迟调用，在下一次中断返回过程中执行
//------------------------------------------------------------------------------

#if 0

// // 每个 CPU 都有自己的延迟调用队列
// // TODO 这个队列放在专门的 work.c 文件中
// static PCPU_DATA spin_t g_work_spin = SPIN_INIT;
// static PCPU_BSS dlnode_t g_work_q;

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

#endif
