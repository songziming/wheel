#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <wheel.h>
#include <arch_api.h>

typedef struct mcs_lock {
    _Atomic size_t tail; // 指向最后一个等待者
} spinlock_t;

#define SPINLOCK_INIT (spinlock_t){0}

// 代表一个锁的获取者/等待者，持有状态也位于链表中
typedef struct mcs_node {
    _Atomic size_t  next; // 最低 bit 用来自旋，=0 表示未得到锁，=1 表示得到锁
    spinlock_t     *lock;
    int             irqkey;
    int         line;
    const char *file;
} mcs_node_t;


#ifdef DEBUG
INIT_TEXT void enable_lockdep();
#else
#define enable_lockdep()
#endif

void mcs_lock_take(spinlock_t *lock, mcs_node_t *node);
void mcs_lock_give(mcs_node_t *node);

#define IRQ_SPINLOCK_TAKE(lock, node)       \
    (node)->irqkey = cpu_int_lock();    \
    mcs_lock_take(lock, node)

#define RAW_SPINLOCK_TAKE(lock, node)   \
    (node)->irqkey = 0; \
    mcs_lock_take(lock, node)

#define SPINLOCK_GIVE(node) \
    mcs_lock_give(node)

/*
使用 cleanup attribute，局部变量退出 scope 的时候，自动执行 lock-give 函数
就像 C++，使用大括号将临界区包围起来即可
*/

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)
#define GUARD_NAME CONCAT(__guard_node, __LINE__)

#define RAW_LOCK_SCOPED(lock) \
    mcs_node_t GUARD_NAME __attribute__((cleanup(mcs_lock_give),unused)); \
    GUARD_NAME.file = __FILE__; \
    GUARD_NAME.line = __LINE__; \
    RAW_SPINLOCK_TAKE(lock, &GUARD_NAME)

#define IRQ_LOCK_SCOPED(lock) \
    mcs_node_t GUARD_NAME __attribute__((cleanup(mcs_lock_give),unused)); \
    GUARD_NAME.file = __FILE__; \
    GUARD_NAME.line = __LINE__; \
    IRQ_SPINLOCK_TAKE(lock, &GUARD_NAME)

#endif // SPINLOCK_H
