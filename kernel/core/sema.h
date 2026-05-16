#ifndef SEMA_H
#define SEMA_H

#include "task.h"

typedef struct sema {
    prioq_t wq;
    int     value;  // guarded by wq->lock
    int     limit;  // guarded by wq->lock
} sema_t;

void sema_init(sema_t *sema, int initial, int limit);
int sema_take(sema_t *sema, int timeout);
void sema_give(sema_t *sema);

#endif // SEMA_H
