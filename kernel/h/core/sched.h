#ifndef CORE_SCHED_H
#define CORE_SCHED_H

#include <base.h>

typedef struct task task_t;

extern __PERCPU task_t * tid_prev;
extern __PERCPU task_t * tid_next;

extern u32  sched_stop(task_t * tid, u32 bits);
extern u32  sched_cont(task_t * tid, u32 bits);
extern void preempt_lock  ();
extern void preempt_unlock();
extern void sched_yield   ();
extern void sched_tick    ();

// requires: kmem, task, per-cpu var
extern __INIT void sched_lib_init();

#endif // CORE_SCHED_H
