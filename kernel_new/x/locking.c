#include <common.h>
#include <arch_intf.h>


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
