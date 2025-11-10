#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <common.h>
#include <library/spin.h>
#include <library/sched_list.h>

typedef struct semaphore {
    spin_t           spin;
    int              limit;
    int              value;
    sched_list_jmp_t penders;
} semaphore_t;

void semaphore_init(semaphore_t *sem, int initial, int max);
int  semaphore_take(semaphore_t *sem, int n, int timeout);
int  semaphore_give(semaphore_t *sem, int n);

#endif // SEMAPHORE_H
