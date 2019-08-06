#ifndef CORE_SEMA_H
#define CORE_SEMA_H

#include <base.h>
#include <core/spin.h>
#include <core/sched.h>
#include <misc/list.h>

// NOTE: no support for priority inversion

typedef struct sema {
    spin_t   spin;
    dllist_t pend_q;
    int      limit;
    int      value;
} sema_t;

extern void sema_init(sema_t * sema, int limit, int value);
extern int  sema_take(sema_t * sema, int timeout);
extern void sema_give(sema_t * sema);

#endif // CORE_SEMA_H
