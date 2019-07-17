#ifndef CORE_TICK_H
#define CORE_TICK_H

#include <base.h>
#include <misc/list.h>

typedef int (* wdog_proc_t) (void * a1, void * a2, void * a3, void * a4);

typedef struct wdog {
    dlnode_t    node;
    int         ticks;
    wdog_proc_t proc;
    void *      arg1;
    void *      arg2;
    void *      arg3;
    void *      arg4;
} wdog_t;

extern void  wdog_init  (wdog_t * dog);
extern void  wdog_start (wdog_t * dog, int ticks, void * proc,
                         void * a1, void * a2, void * a3, void * a4);
extern void  wdog_cancel(wdog_t * dog);
extern void  tick_proc  ();
extern usize tick_get   ();
extern void  tick_delay (int ticks);

#endif // CORE_TICK_H
