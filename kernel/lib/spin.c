#include "spin.h"
#include <arch_api.h>
#include <kstring.h>

void spin_init(spin_t *spin) {
    kmemset(spin, 0, sizeof(spin_t));
}

void raw_spin_take(spin_t *spin) {
    atomic_uint ticket = atomic_fetch_add(&spin->ticket_counter, 1);
    while (atomic_load(&spin->service_counter) != ticket) {
        cpu_pause();
    }
}

void raw_spin_give(spin_t *spin) {
    atomic_fetch_add(&spin->service_counter, 1);
}

int irq_spin_take(spin_t *spin) {
    int key = cpu_int_lock();
    atomic_uint tickket = atomic_fetch_add(&spin->ticket_counter, 1);
    while (atomic_load(&spin->service_counter) != tickket) {
        cpu_int_unlock(key);
        cpu_pause();
        key = cpu_int_lock();
    }
    return key;
}

void irq_spin_give(spin_t *spin, int key) {
    atomic_fetch_add(&spin->service_counter, 1);
    cpu_int_unlock(key);
}


//------------------------------------------------------------------------------
// reader-writer spinlock
//------------------------------------------------------------------------------

// 允许多个 reader，只允许一个 writer
// 也是基于 ticket、service
// 先来先得？还是无条件偏向 writer？

typedef struct rwspin {
    atomic_uint lock; // 最低 bit 表示有 writer，其他 bit 表示 reader 数量
} rwspin_t;

void rwspin_take_reader(rwspin_t *rw) {
    // while (1) {
    //     unsigned old = atomic_load(&rw->lock);
    //     if (old & 1) {
    //         cpu_pause(); // 等待 writer 退出
    //         continue;
    //     }
    //     if (atomic_compare_exchange_strong(&rw->lock, old, old + 2)) {
    //         break;
    //     }
    // }

    // reader 仍在等待，就给 counter+=2，可能让后面的 writer 无法抢占
    unsigned old = atomic_fetch_add(&rw->lock, 2);
    while (old & 1) {
        cpu_pause();
        old = atomic_load(&rw->lock);
    }
}

void rwspin_give_reader(rwspin_t *rw) {
    atomic_fetch_sub(&rw->lock, 2);
}

void rwspin_take_writer(rwspin_t *rw) {
    // while (1) {
    //     unsigned old = atomic_load(&rw->lock);
    //     if (old) {
    //         cpu_pause(); // 仍有 reader 没有退出，或存在其他 writer
    //         continue;
    //     }
    //     if (atomic_compare_exchange_strong(&rw->lock, 0, 1)) {
    //         break;
    //     }
    // }

    while (1) {
        unsigned old = atomic_fetch_or(&rw->lock, 1);
        if (0 == old) {
            break;
        }
        cpu_pause();
    }
}

void rwspin_give_writer(rwspin_t *rw) {
    atomic_fetch_and(&rw->lock, ~1UL);
}



//------------------------------------------------------------------------------
// [WIP] queued spinlock
//------------------------------------------------------------------------------

// 不允许重入
// 一个 CPU 只能持有一个这样的锁
// 适合用在 page-desc 这类需要压缩体积的地方

typedef struct queue_spin {
    atomic_uint val;
} queue_spin_t;


// qspin 结构体中，一个 bit 表示 islocked
#define QSPIN_ISLOCKED  1   // 自旋锁已被持有
#define QSPIN_ISINQUEUE 2   // 队列干净（首次更新）

typedef struct qnode qnode_t;
struct qnode {
    qnode_t    *next;
    atomic_int  hold;
};

static PERCPU_BSS qnode_t g_waiter;

void queue_spin_take(queue_spin_t *qs) {
    qnode_t *waiter = THISCPU(&g_waiter);
    waiter->hold = 0;
    waiter->next = NULL;
    atomic_uint val = (cpu_index() << 1) | 1;  // 最低 bit 表示 islocked

    atomic_uint old = atomic_exchange(&qs->val, val);
    if (old & 1) {
        // 原本处在 locked 状态，需要将自己放入队列末尾
        // 加入队列末尾的过程不是原子的，需要防止此时出现其他的 pender
        qnode_t *tail = PERCPU(old >> 1, &g_waiter);
        tail->next = waiter;
    }
}
