#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <wheel.h>
#include <arch_api.h>

typedef struct spinlock {
    _Atomic size_t tail; // 指向最后一个等待者
} spinlock_t;

#define SPINLOCK_INIT (spinlock_t){0}

// 代表一个锁的获取者/等待者，持有状态也位于链表中
typedef struct spinlock_node {
    _Atomic size_t  next; // 最低 bit 用来自旋，=0 表示未得到锁，=1 表示得到锁
    spinlock_t     *lock;
    int             irqkey;
    int         line;
    const char *file;
} spinlock_node_t;


void spinlock_take(spinlock_t *lock, spinlock_node_t *node);
void spinlock_give(spinlock_node_t *node);


#ifdef DEBUG
    INIT_TEXT void enable_lockdep();
    #define SPINLOCK_TAKE(lock, node) do {  \
        (node)->file = __FILE__;            \
        (node)->line = __LINE__;            \
        spinlock_take(lock, node);          \
    } while (0)
#else // DEBUG
    #define enable_lockdep()
    #define SPINLOCK_TAKE(lock, node) spinlock_take(lock, node)
#endif // DEBUG


// RAII 风格
#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)
#define GUARD_NAME CONCAT(__guard_node, __LINE__)
#define SPINLOCK_SCOPED(lock) \
    spinlock_node_t GUARD_NAME __attribute__((cleanup(spinlock_give),unused)); \
    GUARD_NAME.file = __FILE__; \
    GUARD_NAME.line = __LINE__; \
    spinlock_take(lock, &GUARD_NAME)


#endif // SPINLOCK_H
