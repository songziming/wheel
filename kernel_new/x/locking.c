#include <common.h>
#include <arch_intf.h>


//------------------------------------------------------------------------------
// K42 自旋锁
//------------------------------------------------------------------------------

typedef struct k42_lock k42_lock_t;

struct k42_lock {
    k42_lock_t *next;
    k42_lock_t *tail;
};

void k42_lock(k42_lock_t *lock) {
    k42_lock_t self;
    self.next = NULL;

    // 锁变量（头节点）的 next 指向链表中第一个节点（下一个持有者）
    // 锁变量（头节点）的 tail 指向链表尾（新的等待者放在这里）
    // 节点的 tail 表示是否正在等待

    cpu_rwfence();
    k42_lock_t *pred = (k42_lock_t *)atomic_set((intptr_t *)&lock->tail, (intptr_t)&self);
    if (NULL != pred) {
        self.tail = (void *)1; // 非零
        cpu_rwfence();
        pred->next = &self; // 当前节点放入队列
        cpu_rwfence();
        while (self.tail) { // 自旋等待
            cpu_pause();
        }
    }

    // 至此已经获取自旋锁，但马上 self 变量就会销毁，因为这是局部变量
    // 需要把信息记录在头节点 lock 内部（dequeue）
    // lock->tail = lock，相当于指向头节点自身

    k42_lock_t *succ = self.next;
    if (NULL == succ) { // 后面没有排队的等待者
        cpu_rwfence();
        lock->next = NULL;

        // 自己不再是队尾，说明就在此时又出现了新的 lock-waker
        // lock 流程是先设置 tail，再设置 next，因此需要等 next 字段设置好
        if (atomic_cas((intptr_t *)&lock->tail, (intptr_t)&self, (intptr_t)lock) != (intptr_t)&self) {
            while (!self.next) {
                cpu_pause();
            }
            lock->next = self.next;
        }
    } else {
        lock->next = succ;
    }
}

void k42_unlock(k42_lock_t *lock) {
    k42_lock_t *succ = lock->next;

    cpu_rwfence();

    if (!succ) {
        if (atomic_cas((intptr_t *)&lock->tail, (intptr_t)lock, 0) == (intptr_t)lock) {
            return;
        }
        while (!lock->next) {
            cpu_pause();
        }
        succ = lock->next;
    }
    succ->tail = NULL;
}


//------------------------------------------------------------------------------
// 读写自旋锁
//------------------------------------------------------------------------------

// 读写锁需要提供 upgrade 功能，即持有 reader-lock 直接转换为 writer-lock
// 我们不支持这种用法，只能释放 reader-lock 再重新 lock

// rw spin 相当于两个 ticket spin-lock，只是 ticket counter 共用
// read-lock 尝试获取两个锁，自旋等待读锁，然后立即释放读锁，写锁一直处于尝试获取状态，但是不自旋
// read-unlock 释放写锁
// write-lock 尝试获取两个锁，自选等待写锁
// write-unlock 释放读锁和写锁，必须在一个周期内完成

// 如果 self.ticket == service，说明当前线程持有自旋锁
// self.ticket + 1 == lock.ticket
// 对于锁而言，持有状态下，ticket == service + 1

typedef union rwspin {
    uint32_t u;
    uint16_t us;
    struct {
        unsigned char write; // writer service counter
        unsigned char read;  // reader service counter
        unsigned char users; // ticket counter
    } s;
} rwspin_t;

void rwspin_write_take(rwspin_t *l) {
    uint32_t me = atomic32_add(&l->u, (1 << 16)); // users + 1
    uint8_t val = me >> 16; // old users

    // wait while l->write != old_user
    while (val != (atomic32_get(&l->u) & 0xff)) {
        cpu_pause();
    }
}

void rwspin_write_give(rwspin_t *l) {
    uint32_t old;
    rwspin_t update;

    // unlock 操作需要增加两个字段，每个字段要各自加一，分别溢出，不能整体加 0x101
    // 只能用 cas-loop
    do {
        old = atomic32_get(l);
        update.u = old;
        ++update.s.write;
        ++update.s.read;
    } while (atomic32_cas(&l->u, old, update.u) != old);
}

int rwspin_write_trytake(rwspin_t *l) {
    rwspin_t old;
    rwspin_t update;

    old.u = atomic32_get(&l->u);
    old.s.write = old.s.users;

    // service + 1 == ticket，说明我们成功持有锁
    update.u = old.u;
    ++update.s.users;

    if (atomic32_cas(&l->u, old.u, update.u) == old.u) {
        return 0; // 成功获得锁
    }

    // lock 原本的值发生了变化，未得到锁
    return 1;
}

void rwspin_read_take(rwspin_t *l) {
    uint32_t me = atomic32_add(&l->u, (1 << 16)); // users + 1
    uint8_t val = me >> 16; // old users

    while (val != ((atomic32_get(&l->u) >> 8) & 0xff)) {
        cpu_pause();
    }

    // 释放读锁
    atomic8_add(&l->s.read, 1);
}

void rwspin_read_give(rwspin_t *l) {
    atomic8_add(&l->s.write, 1);
}

int rwspin_read_trytake(rwspin_t *l) {
    rwspin_t cmp;
    rwspin_t update;

    cmp.u = atomic32_get(&l->u);
    cmp.s.read = cmp.s.users;
    update.u = cmp.u;
    ++update.s.read;
    ++update.s.users;

    if (cmp.u == atomic32_cas(&l->u, cmp.u, update.u)) {
        return 0;
    }

    return 1;
}
