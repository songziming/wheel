#include <dllist.h>

// 类似于 ktimer，也是异步执行函数，也是在中断返回流程里执行
// 但 workq 不关心时间，只需保证在 ISR 上下文运行
// 而且 workq 是每个CPU 都有的，不需要争抢，默认放在当前 cpu 的队列中

// 某些操作需要破坏当前栈，因此不能在 task 上下文执行
// 这就需要注册 work，在下一次中断里执行函数，因为此时已经不处于 task 上下文

typedef struct work work_t;

struct work {
    dlnode_t dl;
    void (*func)(work_t *self);
};

void work_defer(work_t *wk, void *func) {
    dl_init_circular(&wk->dl);
    wk->func = func;
}
