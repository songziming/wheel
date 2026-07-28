#ifndef MUTEX_H
#define MUTEX_H

#include <wheel.h>

// 互斥锁，opaque handle，结构体定义在 mutex.c
// 由 kobj 动态分配，引用计数管理生命周期
typedef struct mutex mutex_t;

INIT_TEXT void  mutex_init(void);
mutex_t *mutex_make(const char *name);
int      mutex_take(mutex_t *mut, int timeout); // 1=成功, 0=超时
void     mutex_give(mutex_t *mut);
void     mutex_drop(mutex_t *mut);

#endif // MUTEX_H
