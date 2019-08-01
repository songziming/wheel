#ifndef CORE_SCHED_H
#define CORE_SCHED_H

#include <base.h>
#include <core/task.h>
#include <misc/list.h>

typedef struct task task_t;

typedef struct wait_q {
    spin_t   spin;
    dllist_t waiters;
} wait_q_t;

typedef struct waiter {
    dlnode_t dl;
    int      up;
    task_t * tid;
} waiter_t;

#define WAIT_Q_INIT ((wait_q_t) { SPIN_INIT, DLLIST_INIT })

// typedef struct turnstile {
//     int      count;
//     u32      priorities;
//     dllist_t tasks[PRIORITY_COUNT];
// } turnstile_t;

// extern void     turnstile_init  (turnstile_t * ts);
// extern void     turnstile_push  (turnstile_t * ts, task_t * tid);
// extern void     turnstile_remove(turnstile_t * ts, task_t * tid);
// extern task_t * turnstile_peek  (turnstile_t * ts);
// extern task_t * turnstile_pop   (turnstile_t * ts);

extern __PERCPU int      no_preempt;
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
