#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <def.h>
#include "spin.h"
// #include <dllist.h>
#include "sched.h"

typedef struct semaphore {
    spin_t   spin;
    int      limit;
    int      value;
    priority_q_t penders;
} semaphore_t;

void semaphore_init(semaphore_t *sem, int n, int max);
void semaphore_take(semaphore_t *sem, int n, int timeout);
void semaphore_give(semaphore_t *sem, int n);

#endif // SEMAPHORE_H
