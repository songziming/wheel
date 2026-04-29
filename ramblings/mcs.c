#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>


static inline void cpu_pause() {
    __asm__ volatile("pause");
}

//------------------------------------------------------------------------------
// dummy spinlock
//------------------------------------------------------------------------------

typedef struct dummy_lock {
    _Atomic uint32_t value;
} dummy_lock_t;

void dummy_lock_take(dummy_lock_t *lock) {
    while (1) {
        uint32_t old = atomic_exchange(&lock->value, 1);
        if (0 == old) {
            return;
        }
        cpu_pause();
    }
}

void dummy_lock_give(dummy_lock_t *lock) {
    uint32_t old = atomic_exchange(&lock->value, 0);
    if (0 == old) {
        fprintf(stderr, "dummy lock is not locked!\n");
    }
}


//------------------------------------------------------------------------------
// MCS-lock，持有过程中需要保留 holder 变量
//------------------------------------------------------------------------------

// 获得锁之后，holder 仍然留在队列中

// MCS-take 过程，可能存在中间状态
// 统一先更新 lock->tail，然后再更新 holder->next
// 如果发现 lock->tail 变化，不能直接读 holder->next，需要等待 next 非零

typedef struct mcs_holder {
    _Atomic size_t next; // 最低 bit 用来自旋，=0 表示未得到锁，=1 表示得到锁
} mcs_holder_t;
typedef struct mcs_lock {
    _Atomic size_t tail; // 指向最后一个等待者
} mcs_lock_t;

void mcs_lock_take(mcs_lock_t *lock, mcs_holder_t *holder) {
    holder->next = 0;
    mcs_holder_t *prev = (mcs_holder_t*)atomic_exchange(&lock->tail, holder);
    if (NULL == prev) {
        // 没有前驱，成功获得锁
        return;
    }

    // 有前驱节点，把自己连接到前驱之后，注意不能触碰最低 bit
    atomic_fetch_or(&prev->next, (size_t)holder);

    // 自旋，不断检查最低 bit
    while (0 == (atomic_load(&holder->next) & 1)) {
        cpu_pause();
    }
}

void mcs_lock_give(mcs_lock_t *lock, mcs_holder_t *holder) {
    mcs_holder_t *tail = (mcs_holder_t*)atomic_compare_exchange_strong(&lock->tail, holder, 0);
    if (tail == holder) {
        // 自己仍是最后一个，成功释放锁
        return;
    }

    // 自己不是最后节点，需要通知后继
    // 后继线程更新 lock->tail 和 holder->next 是两步执行的
    // 可能刚刚更新 tail，尚未更新 next。所以我们需要等待
    mcs_holder_t *next;
    while (NULL == (next = (mcs_holder_t*)atomic_load(&holder->next))) {
        cpu_pause();
    }

    // 后继节点的最低 bit 置 1，通知后继线程获得锁
    atomic_fetch_or(&next->next, 1);
}

//------------------------------------------------------------------------------
// K42-lock，类似 mcs 但不需要传入 holder
//------------------------------------------------------------------------------

// Linux kernel 代码规模大，mcs-lock 改变了 API，不容易推广
// K42 可以保持 API 一致，推广容易（我们没有这个问题，可以放心使用 mcs）

// take 函数自己创建 holder，获得锁之后 holder 就析构了
// 相当于 mcs-lock-give 一部分逻辑放在了 k42-lock-take 里面

// holder->next 指向后继等待者，一旦获得锁，holder 就不存在了
// 所以要把 holder->next 保存到 k42-lock 里面
// 也可以认为，k42-lock 里面放了一个公共的 holder，谁持有锁，谁就使用这个 holder

typedef struct k42_holder {
    _Atomic size_t next; // last bit 用于自旋，=0 表示未获得锁，=1 表示拥有锁
} k42_holder_t;
typedef struct k42_lock {
    _Atomic size_t tail; // 最后一个等待者
    k42_holder_t   owner; // 当前锁的持有者
} k42_lock_t;

void k42_lock_take(k42_lock_t *lock) {
    // 临时 holder，自旋状态使用这个 holder
    k42_holder_t holder = {0};

    k42_holder_t *prev = (k42_holder_t*)atomic_exchange(&lock->tail, &holder);
    if (NULL != prev) {
        // 存在前驱节点，需要自旋等待
        atomic_fetch_or(&prev->next, (size_t)&holder);
        while (0 == (atomic_load(&holder.next) & 1)) {
            cpu_pause();
        }
    }

    // 得到了锁，但是 holder 即将消除，应该替换成 lock->holder
    // 这样，持有锁的时候，如果新的线程到来，会把自己注册到
    lock->owner.next = 0UL;
    k42_holder_t *oldtail = (k42_holder_t*)atomic_compare_exchange_strong(&lock->tail, &holder, &lock->owner);
    if (oldtail != &holder) {
        // 我们不再是队列末尾！等待后继节点设置好 next 字段
        // lock->tail 已经指向新的尾节点，我们无需更新 tail
        k42_holder_t *next;
        while (0 == (next = (k42_holder_t*)atomic_load(&holder.next))) {
            cpu_pause();
        }
        lock->owner.next = next;
    } else {
        // 没有新的后继节点到来，我们仍是最后一个 holder
        // lock->tail 成功指向 lock->holder
    }
}

void k42_lock_give(k42_lock_t *lock) {
    k42_holder_t *oldtail = (k42_holder_t*)atomic_compare_exchange_strong(&lock->tail, &lock->owner, 0);
    if (oldtail != &lock->owner) {
        // 我们不是末尾，存在后继节点，等待后继的指针设置好
        k42_holder_t *next;
        while (NULL == (next = atomic_load(&lock->owner.next))) {
            cpu_pause();
        }

        // 告知后继得到了锁
        atomic_fetch_or(&next->next, 1);
    }
}
