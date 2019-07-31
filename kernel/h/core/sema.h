#ifndef CORE_SEMA_H
#define CORE_SEMA_H

#include <base.h>
#include <core/spin.h>
#include <core/sched.h>
#include <misc/list.h>

// IMPORTANT: after freeall, sema is not safe to delete
//            parent struct should use ref-counting to manage life cycle

// NOTE: no support for priority inversion

typedef struct sema {
    spin_t   spin;
    dllist_t pend_q;
    int      limit;
    int      value;
} sema_t;

#define SEMA_WAIT_FOREVER ((int) -1)

extern void sema_init   (sema_t * sem, int limit, int value);
// extern void sema_freeall(sema_t * sem);
extern int  sema_take   (sema_t * sem, int timeout);
extern int  sema_trytake(sema_t * sem);
extern void sema_give   (sema_t * sem);

#endif // CORE_SEMA_H
