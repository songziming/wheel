// WIP
// 新的自旋锁
// 基于 MCS-lock，结合了 lockdep

#include "spinlock.h"
#include <arch_api.h>
#include <debug.h>


// 记录spinlock持有栈，即当前CPU持有了多少自旋锁
// 自旋锁是短时锁，不可能持有锁的时候切换任务（但允许持有锁的时候中断，但这仍是相同CPU）
// 所以，记录持有锁完全可以使用 percpu-var，切换任务的时候还可以检查 held_size 是否为零
PERCPU_DATA int g_held_size = 0;


#ifdef DEBUG

#define MAX_LOCK_ALLOWED 8
static PERCPU_BSS mcs_node_t *g_held[MAX_LOCK_ALLOWED];
static CONST int lockdep_on = 0;

INIT_TEXT void enable_lockdep() {
    lockdep_on = 1;
}

#endif // DEBUG



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

#ifdef DEBUG
    if (lockdep_on) {
        // 这个是 percpu-var，不会和其他 CPU 竞争，除了中断
        // 保证此 CPU 之上原子性即可，不需要 lock 前缀
        int depth = THISCPU_XADD(g_held_size, 1);
        if (depth >= MAX_LOCK_ALLOWED) {
            panic("held lock overflow!\n");
        }
        for (int i = 0; i < depth; ++i) {
            if (g_held[i]->lock == lock) {
                panic("already has same lock\n");
            }
        }
        g_held[depth] = node;
    }
#endif
}

void mcs_lock_give(mcs_node_t *node) {
#ifdef DEBUG
    int depth = THISCPU_XADD(g_held_size, -1) - 1;
    if ((depth >= 0) && (g_held[depth]->lock != node->lock)) {
        panic("release wrong lock\n");
    }
#endif
    mcs_lock_t *lock = node->lock;
    size_t expected = (size_t)node;
    if (!atomic_compare_exchange_strong(&lock->tail, &expected, 0)) {

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

    cpu_int_unlock(node->irqkey);
}
