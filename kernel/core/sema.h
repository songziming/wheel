#ifndef SEMA_H
#define SEMA_H

#include "task.h"

typedef struct sema {
    spinlock_t  lock;
    prioq_t wq;
    int     value;  // guarded by wq.lock
    int     limit;  // guarded by wq.lock
} sema_t;

void sema_init(sema_t *sema, int initial, int limit);
int  sema_take(sema_t *sema, int timeout); // return 1 if success, 0 if timeout
void sema_give(sema_t *sema);

#endif // SEMA_H
