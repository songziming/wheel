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
