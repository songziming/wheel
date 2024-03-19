#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <def.h>
#include "spin.h"
#include <dllist.h>

typedef struct semaphore {
    spin_t   spin;
    dlnode_t pend_q;
    int      limit;
    int      value;
} semaphore_t;

void semaphore_init(semaphore_t *sem, int n, int max);
void semaphore_take(semaphore_t *sem, int n, int timeout);
void semaphore_give(semaphore_t *sem, int n);

#endif // SEMAPHORE_H
