// WIP
// 新的自旋锁
// 基于 MCS-lock，结合了 lockdep

#include "spinlock.h"
#include <arch_api.h>


// 记录spinlock持有栈，即当前CPU持有了多少自旋锁
// 自旋锁是短时锁，不可能持有锁的时候切换任务（但允许持有锁的时候中断，但这仍是相同CPU）
// 所以，记录持有锁完全可以使用 percpu-var，切换任务的时候还可以检查 held_size 是否为零
#define MAX_LOCK_ALLOWED 8
static PERCPU_BSS mcs_node_t *g_held[MAX_LOCK_ALLOWED];
PERCPU_DATA int g_held_size = 0;




void mcs_lock_take(mcs_lock_t *lock, mcs_node_t *node) {
    node->next = 0;
    node->lock = lock;

    mcs_node_t *prev = (mcs_node_t*)atomic_exchange(&lock->tail, (size_t)node);
    if (NULL != prev) {
        // 有前驱节点，把自己连接到前驱之后，注意不能触碰最低 bit
        atomic_fetch_or(&prev->next, (size_t)node);

        // 自旋，不断检查最低 bit
        while (0 == (atomic_load(&node->next) & 1)) {
            cpu_pause();
        }
    }

#ifdef LOCKDEP
    // 应该使用 fetch-and-add，但应该没有其他线程与我们争抢（除了中断）
    int depth = THISCPU_GET(g_held_size);
    g_held[depth] = node;
    THISCPU_ADD(g_held_size, 1);
#endif
}

void mcs_lock_give(mcs_node_t *node) {
    mcs_lock_t *lock = node->lock;
    size_t expected = (size_t)node;
    if (atomic_compare_exchange_strong(&lock->tail, &expected, 0)) {
        return; // 自己仍是最后一个，成功释放锁
    }

    // 自己不是最后节点，需要通知后继
    // 后继线程更新 lock->tail 和 node->next 是两步执行的
    // 可能刚刚更新 tail，尚未更新 next。所以我们需要等待
    mcs_node_t *next;
    while (NULL == (next = (mcs_node_t*)atomic_load(&node->next))) {
        cpu_pause();
    }

    // 后继节点的最低 bit 置 1，通知后继线程获得锁
    atomic_fetch_or(&next->next, 1);
}
