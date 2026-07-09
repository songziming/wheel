#ifndef MSGQ_H
#define MSGQ_H

#include "task.h"
#include <fifo.h>
#include <spinlock.h>
#include <vmspace.h>

typedef struct msgq {
    spinlock_t  lock;
    vmrange_t   rng;        // 交换数据用的缓冲区
    fifo_t      fifo;
    prioq_t     readers;    // 阻塞的读者
    prioq_t     writers;    // 阻塞的写者
} msgq_t;

void msgq_init(msgq_t *q);
size_t msgq_send(msgq_t *q, const void *msg, size_t len, int timeout);
void msgq_send_force(msgq_t *q, void *msg, size_t len);
size_t msgq_recv(msgq_t *q, void *dst, size_t len, int timeout);

#endif // MSGQ_H
