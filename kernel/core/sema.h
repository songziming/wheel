#ifndef SEMA_H
#define SEMA_H

#include <wheel.h>

// 信号量，opaque handle，结构体定义在 sema.c
// 由 kobj 动态分配，引用计数管理生命周期
typedef struct sema sema_t;

INIT_TEXT void  sema_init(void);
sema_t *sema_make(const char *name, int initial, int limit);
int     sema_take(sema_t *sema, int timeout); // 1=成功, 0=超时
void    sema_give(sema_t *sema);
void    sema_drop(sema_t *sema);

#endif // SEMA_H
