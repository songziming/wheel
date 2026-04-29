#include <wheel.h>

// K42 lock
// 每个线程创建自己的 spinner，组成一个链表
// 自旋状态的线程才有 spinner，持有锁的线程没有 spinner

typedef struct k42 k42_t;

struct k42 {
    _Atomic k42_t *next;
    _Atomic k42_t *tail;
};

// 自选队列是单链表
// 自旋锁（头）节点：
//  - next 指向自选队列的开头，也就是队列中第一个等待者
//  - tail 指向队尾，也就是最后一个等待者
// 等待线程的 spinner 节点：
//  - next 指向下一个节点
//  - tail 是自旋检查的字段
// TODO 自旋位可以和 next 指针结合在一起

// 获取锁的过程
//  1. 将新节点放入队列末尾，通过交换 lock->tail，同时获得原来的 old_tail
//  2. 修改 old_tail->next，指向自己

void k42_take(k42_t *lock) {
    // 栈上创建节点
    k42_t spinner;

    // 放入队列结尾
    k42_t *prev = atomic_exchange(&lock->tail, &spinner);

    // 如果存在前驱节点，说明锁被其他线程持有，需要自旋
    if (prev) {
        // 自旋过程，就是不断检查 last
        spinner.tail = (void*)16;

        // 让前驱节点指向自己，这样前一个线程释放锁时，才能唤醒自己
        cpu_wfence();
        prev->next = &spinner;

        // 开始自旋等待
        cpu_wfence();
        while (spinner.tail) {
            cpu_pause();
        }
    }

    // 成功获取到锁，需要将 spinner 移除队列
    // 指向下一个自旋状态的 spinner
    k42_t *next = atomic_load(&spinner.next);
    if (NULL == next) {
        // 没有后继元素
        // 让 spinlock 指向 spinlock 本身
        lock->next = NULL;
        if (atomic_compare_exchange(&lock->tail, &spinner, lock) != &spinner) {
            // 如果又发现 tail 不再指向自己，说明这时候来了新的后继
            // 自己已不是最后一个 taker，等待后继把自己的 next 设置好
            while ((next = atomic_load(&spinner.next)) == NULL) {
                cpu_pause();
            }
            lock->next = next;
        }
    }
}
