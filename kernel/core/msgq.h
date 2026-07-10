#ifndef MSGQ_H
#define MSGQ_H

#include <wheel.h>

// 消息队列，opaque handle，结构体定义在 msgq.c
// 由 kobj 动态分配，引用计数管理生命周期
typedef struct msgq msgq_t;

INIT_TEXT void  msgq_init(void);
msgq_t *msgq_make(const char *name);
size_t  msgq_send(msgq_t *q, const void *msg, size_t len, int timeout);
void    msgq_send_force(msgq_t *q, void *msg, size_t len);
size_t  msgq_recv(msgq_t *q, void *dst, size_t len, int timeout);
void    msgq_drop(msgq_t *q);

#endif // MSGQ_H
