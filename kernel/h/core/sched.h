#ifndef CORE_SCHED_H
#define CORE_SCHED_H

#include <base.h>
#include <core/task.h>
#include <misc/list.h>

typedef struct task task_t;

typedef struct turnstile {
    u32      priorities;
    dllist_t tasks[PRIORITY_COUNT];
} turnstile_t;

#define TURNSTILE_INIT ((turnstile_t) { 0U, { DLLIST_INIT } })

extern void     turnstile_put(task_t * tid);
extern task_t * turnstile_top();
extern task_t * turnstile_pop();

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
