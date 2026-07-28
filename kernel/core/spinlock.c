// WIP
// 新的自旋锁
// 基于 MCS-lock，结合了 lockdep

#include "spinlock.h"
#include <arch_api.h>
#include <debug.h>


#ifdef DEBUG

// 记录spinlock持有栈，即当前CPU持有了多少自旋锁
// 自旋锁是短时锁，不可能持有锁的同时切换任务，也不能中断
// 所以，记录持有锁完全可以使用 percpu-var
#define MAX_LOCK_ALLOWED 8
static PERCPU_DATA int g_held_size = 0;
static PERCPU_BSS spinlock_node_t *g_held[MAX_LOCK_ALLOWED];

// lockdep 需要用到 percpu-var，不是开机就能使用的
// 需要等 percpu/thiscpu 配置好，才能开启 lockdep
static CONST int lockdep_on = 0;
INIT_TEXT void enable_lockdep() {
    lockdep_on = 1;
}

#endif // DEBUG



void spinlock_take(spinlock_t *lock, spinlock_node_t *node) {
    node->next = 0;
    node->lock = lock;
    node->irqkey = cpu_int_disable();

    spinlock_node_t *prev = (spinlock_node_t*)atomic_exchange(&lock->tail, (size_t)node);
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
        THISCPU_SET(g_held[depth], node);

        for (int i = 0; i < depth; ++i) {
            if (THISCPU_GET(g_held[i])->lock == lock) {
                panic("already has same lock, taken at %s:%d\n",
                    THISCPU_GET(g_held[i])->file,
                    THISCPU_GET(g_held[i])->line);
            }
        }
    }
#endif
}

void spinlock_give(spinlock_node_t *node) {
#ifdef DEBUG
    if (lockdep_on) {
        int depth = THISCPU_XADD(g_held_size, -1) - 1;
        if (depth < 0) {
            panic("not holding any lock!\n");
        } else if (THISCPU_GET(g_held[depth])->lock != node->lock) {
            panic("release wrong lock, last lock at %s:%d\n",
                THISCPU_GET(g_held[depth])->file,
                THISCPU_GET(g_held[depth])->line);
        }
    }
#endif

    spinlock_t *lock = node->lock;
    size_t expected = (size_t)node;
    if (!atomic_compare_exchange_strong(&lock->tail, &expected, 0)) {
        // 自己不是最后节点，需要通知后继
        // 后继线程更新 lock->tail 和 node->next 是两步执行的
        // 可能刚刚更新 tail，尚未更新 next。所以我们需要等待
        spinlock_node_t *next;
        while (NULL == (next = (spinlock_node_t*)atomic_load(&node->next))) {
            cpu_pause();
        }

        // 后继节点的最低 bit 置 1，通知后继线程获得锁
        atomic_fetch_or(&next->next, 1);
    }

    cpu_int_restore(node->irqkey);
}
