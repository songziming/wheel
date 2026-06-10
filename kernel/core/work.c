#include "work.h"
#include <arch_api.h>
#include <spinlock.h>
#include <debug.h>

// 类似于 wdog，也是异步执行函数，也是在中断返回流程里执行
// 但 workq 不关心时间，只需保证在 ISR 上下文运行
// 而且 workq 是每个CPU 都有的，不需要争抢，默认放在当前 cpu 的队列中

// 某些操作需要破坏当前栈，因此不能在 task 上下文执行
// 这就需要注册 work，在下一次中断里执行函数，因为此时已经不处于 task 上下文

// work-q 严格 percpu，不会跨 cpu 访问，只有任务和 ISR 两个上下文
// 不需要自旋锁，禁用中断就可以保证安全

// static PERCPU_DATA spin_t g_work_lock = SPIN_INIT;
static PERCPU_BSS dlnode_t g_work_q;

INIT_TEXT void work_init_this() {
    dlnode_t *head = THISCPU(&g_work_q);
    dl_init_circular(head);
}

// 注册一个异步任务，放在队列中
void work_defer(work_t *wk, work_cb_t func, const char *desc) {
    wk->func = func;
    wk->desc = desc;

    int key = cpu_int_lock();
    // spin_t *lock = THISCPU(&g_work_lock);
    // int key = irq_spin_take(lock);

    ASSERT(!dl_contains(THISCPU(&g_work_q), &wk->dl));
    dl_insert_before(&wk->dl, THISCPU(&g_work_q));

    cpu_int_unlock(key);
    // irq_spin_give(lock, key);
}

// 在中断返回过程中执行，只有最外层中断返回时执行
// 此时中断仍禁用，无需获取锁
void work_flush() {
    int key = cpu_int_lock();
    // spin_t *lock = THISCPU(&g_work_lock);
    // int key = irq_spin_take(lock);

    dlnode_t *head = THISCPU(&g_work_q);
    dlnode_t *node = head->next;
    dl_init_circular(head); // 首先把队列清空，work 里面还可以注册下一个 work
    while (node != head) {
        work_t *work = containerof(node, work_t, dl);
        node = node->next; // 执行函数之后，work所在内存可能就不存在了
        work->func(work);
    }

    cpu_int_unlock(key);
    // irq_spin_give(lock, key);
}
