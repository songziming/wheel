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

#define SEMA_FULL(x) ((sema_t) { SPIN_INIT, DLLIST_INIT, (x), (x) })
#define SEMA_ZERO(x) ((sema_t) { SPIN_INIT, DLLIST_INIT, (x), (0) })

extern int  sema_take(sema_t * sema, int timeout);
extern void sema_give(sema_t * sema);

#endif // CORE_SEMA_H
