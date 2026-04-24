#include "work.h"
#include <arch_api.h>
#include <spin.h>

// 类似于 ktimer，也是异步执行函数，也是在中断返回流程里执行
// 但 workq 不关心时间，只需保证在 ISR 上下文运行
// 而且 workq 是每个CPU 都有的，不需要争抢，默认放在当前 cpu 的队列中

// 某些操作需要破坏当前栈，因此不能在 task 上下文执行
// 这就需要注册 work，在下一次中断里执行函数，因为此时已经不处于 task 上下文

// work-q 严格 percpu，不会跨 cpu 访问，只有任务和 ISR 两个上下文
// 不需要自旋锁，禁用中断就可以保证安全

// static PERCPU_BSS spin_t g_work_lock;
static PERCPU_BSS dlnode_t g_work_q;

INIT_TEXT void work_init_this() {
    // spin_t *lock = THISCPU(&g_work_lock);
    dlnode_t *head = THISCPU(&g_work_q);
    // spin_init(lock);
    dl_init_circular(head);
}

// 注册一个异步任务，放在队列中
// TODO workq 严格 percpu，只有任务和ISR两个上下文
//      不需要自旋锁，禁用中断就可以保证安全
void work_defer(work_t *wk, void *func) {
    dl_init_circular(&wk->dl);
    wk->func = func;

    int key = cpu_int_lock();
    dl_insert_before(&wk->dl, THISCPU(&g_work_q));
    cpu_int_unlock(key);
}

// 在中断返回过程中执行，只有最外层中断返回时执行
// 此时中断仍禁用，无需获取锁
void work_flush() {
    dlnode_t *head = THISCPU(&g_work_q);
    dlnode_t *node = head->next;
    dl_init_circular(head); // 首先把队列清空，work 里面还可以注册下一个 work
    for (; node != head; node = node->next) {
        work_t *work = containerof(node, work_t, dl);
        work->func(work);
    }
}
