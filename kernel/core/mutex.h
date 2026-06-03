#ifndef MUTEX_H
#define MUTEX_H

#include "task.h"

typedef struct mutex {
    spin_t  lock;
    prioq_t wq;
    task_t *owner;  // guarded by wq.lock, NULL if not locked
} mutex_t;

void mutex_init(mutex_t *mut);
int  mutex_take(mutex_t *mut, int timeout); // return 1 if success, 0 if timeout
void mutex_give(mutex_t *mut);

#endif // MUTEX_H
