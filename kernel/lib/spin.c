#include "spin.h"
#include <arch_api.h>
#include <kstring.h>


// void spin_init(spin_t *spin) {
//     kmemset(spin, 0, sizeof(spin_t));
// }

void raw_spin_take(spin_t *spin) {
    uint32_t ticket = atomic_fetch_add(&spin->ticket_counter, 1);
    while (atomic_load(&spin->service_counter) != ticket) {
        cpu_pause();
    }
    lockdep_acquire(spin, &spin->dep);
}

void raw_spin_give(spin_t *spin) {
    lockdep_release(spin, &spin->dep);
    atomic_fetch_add(&spin->service_counter, 1);
}

int irq_spin_take(spin_t *spin) {
    int key = cpu_int_lock();
    uint32_t tickket = atomic_fetch_add(&spin->ticket_counter, 1);
    while (atomic_load(&spin->service_counter) != tickket) {
        cpu_pause();
    }
    lockdep_acquire(spin, &spin->dep);
    return key;
}

void irq_spin_give(spin_t *spin, int key) {
    lockdep_release(spin, &spin->dep);
    atomic_fetch_add(&spin->service_counter, 1);
    cpu_int_unlock(key);
}



// 紧凑型自旋锁
typedef union minispin {
    _Atomic uint32_t u;
    struct {
        _Atomic uint16_t ticket;
        _Atomic uint16_t service;
    };
} minispin_t;
void minispin_take(minispin_t *spin) {
    uint16_t ticket = atomic_fetch_add(&spin->ticket, 1);
    while (atomic_load(&spin->service) != ticket) {
        cpu_pause();
    }
}
void minispin_give(minispin_t *spin) {
    atomic_fetch_add(&spin->service, 1);
}
int minispin_islocked(minispin_t *spin) {
    uint32_t ticket_service = atomic_load(&spin->u);
    uint32_t ticket  = ticket_service & 0xffff;
    uint32_t service = ticket_service >> 16;
    return (ticket == service) ? 1 : 0;
}


//------------------------------------------------------------------------------
// reader-writer spinlock
//------------------------------------------------------------------------------

// void rwspin_init(rwspin_t *rw) {
//     spin_init(&rw->spin);
//     rw->reader_num = 0;
// }

void rwspin_take_writer(rwspin_t *rw) {
    lockdep_acquire(rw, &rw->dep);
    raw_spin_take(&rw->spin);   // 获取写权限
    while (0 != atomic_load(&rw->reader_num)) { // 等待所有 reader 结束
        cpu_pause();
    }
}

void rwspin_give_writer(rwspin_t *rw) {
    raw_spin_give(&rw->spin);
    lockdep_release(rw, &rw->dep);
}

void rwspin_take_reader(rwspin_t *rw) {
    lockdep_acquire(rw, &rw->dep);
    raw_spin_take(&rw->spin); // 临时获取唯一锁

    // 如果成功得到了读锁，说明此时没有 writer
    // 可以给 reader 计数器加一，释放读锁
    atomic_fetch_add(&rw->reader_num, 1);
    raw_spin_give(&rw->spin);
}

void rwspin_give_reader(rwspin_t *rw) {
    atomic_fetch_sub(&rw->reader_num, 1);
    lockdep_release(rw, &rw->dep);
}


// 获取读写锁同时关闭中断
int irqrw_take_writer(rwspin_t *rw) {
    lockdep_acquire(rw, &rw->dep);
    int key = irq_spin_take(&rw->spin);
    while (0 != atomic_load(&rw->reader_num)) { // 等待所有 reader 结束
        cpu_pause();
    }
    return key;
}
int irqrw_take_reader(rwspin_t *rw) {
    lockdep_acquire(rw, &rw->dep);
    int key = irq_spin_take(&rw->spin);
    atomic_fetch_add(&rw->reader_num, 1);
    raw_spin_give(&rw->spin);
    return key;
}
void irqrw_give_writer(rwspin_t *rw, int key) {
    irq_spin_give(&rw->spin, key);
    lockdep_release(rw, &rw->dep);
}
void irqrw_give_reader(rwspin_t *rw, int key) {
    atomic_fetch_sub(&rw->reader_num, 1);
    lockdep_release(rw, &rw->dep);
    cpu_int_unlock(key);
}


//------------------------------------------------------------------------------
// slim read-write locks
//------------------------------------------------------------------------------

// 这是 Windows 提供的读写锁，实现参考 ReactOS
// 类似 MCS-lock，每个线程都有自己的局部变量
typedef struct srwlock {
    uintptr_t   p;
} srwlock_t;

typedef struct srw_waiter srw_waiter_t;
struct srw_waiter {
    uintptr_t spin;
    srw_waiter_t *next;
};




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
