#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <spin.h>
#include <dllist.h>

typedef struct semaphore {
    spin_t   lock;
    int      limit;
    int      value;
    dlnode_t penders;
} semaphore_t;

void semaphore_init(semaphore_t *sem, int initial, int max);
int  semaphore_take(semaphore_t *sem, int n, int timeout);
void semaphore_give(semaphore_t *sem, int n);

#endif // SEMAPHORE_H
