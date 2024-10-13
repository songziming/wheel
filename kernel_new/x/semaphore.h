#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <common.h>
#include <library/spin.h>
// #include <proc/sched.h>
#include <library/sched_list.h>

typedef struct semaphore {
    spin_t       spin;
    int          limit;
    int          value;
    sched_list_t penders;
} semaphore_t;

typedef struct fifo_semaphore {
    semaphore_t common;
    dlnode_t    penders;
} fifo_semaphore_t;

typedef struct priority_semaphore {
    semaphore_t common;
    sched_list_t penders;
} priority_semaphore_t;

void fifo_semaphore_init(fifo_semaphore_t *sem, int initial, int max);
int  fifo_semaphore_take(fifo_semaphore_t *sem, int n, int timeout);
int  fifo_semaphore_give(fifo_semaphore_t *sem, int n);

void priority_semaphore_init(priority_semaphore_t *sem, int initial, int max);
int  priority_semaphore_take(priority_semaphore_t *sem, int n, int timeout);
int  priority_semaphore_give(priority_semaphore_t *sem, int n);

#endif // SEMAPHORE_H
