#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <wheel.h>

typedef struct mcs_lock {
    _Atomic size_t tail; // 指向最后一个等待者
} mcs_lock_t;

// 代表一个锁的持有者，持有状态也位于链表中
typedef struct mcs_node {
    _Atomic size_t  next; // 最低 bit 用来自旋，=0 表示未得到锁，=1 表示得到锁
    mcs_lock_t     *lock;
    int             irqkey;
#if defined(LOCKDEP)
    int         line;
    const char *file;
#endif
} mcs_node_t;


extern PERCPU_DATA int g_held_size;

void mcs_lock_take(mcs_lock_t *lock, mcs_node_t *node);
void mcs_lock_give(mcs_node_t *node);


#ifdef LOCKDEP

INIT_TEXT void lockdep_enable();

#define SPIN_TAKE(lock, node) do {  \
    (node)->irqkey = -1;        \
    (node)->file = __FILE__;    \
    (node)->line = __LINE__;    \
    mcs_lock_take(lock, node);  \
} while (0)

#else // LOCKDEP

#define SPIN_TAKE(lock, node)   mcs_lock_take(lock, node)

#endif // LOCKDEP


/*
使用 cleanup attribute，局部变量退出 scope 的时候，自动执行 lock-give 函数
就像 C++，使用大括号将临界区包围起来即可
*/

#define SPIN_GUARD(lock) \
    mcs_node_t __guard_node __attribute__(__cleanup__(mcs_lock_give));  \
    SPIN_TAKE(lock, &__guard_node)


#endif // SPINLOCK_H
