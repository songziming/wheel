#ifndef CORE_TASK_H
#define CORE_TASK_H

#include <base.h>
#include <arch.h>

#include <core/spin.h>
#include <misc/list.h>

typedef struct task {
    regs_t      regs;           // arch-specific context
    spin_t      lock;
    dlnode_t    dl_sched;       // node in ready queue

    u32         state;
    int         priority;
    cpuset_t    affinity;
    int         last_cpu;
    int         timeslice;
    int         remaining;

    pfn_t       kstack;         // kernel stack, one block
} task_t;

// task priorities
#define PRIORITY_COUNT  32      // we use `u32` as priority bitmask
#define PRIORITY_IDLE   31      // lowest priority = idle
#define PRIORITY_NONRT  30      // 2nd lowest priority = non-real-time

// task states
#define TS_READY        0x00    // running or runnable, in ready_q
#define TS_PEND         0x01    // waiting for something, in pend_q
#define TS_DELAY        0x02    // task delay or timeout, wdog active
#define TS_SUSPEND      0x04    // stopped on purpose, not on any q
#define TS_ZOMBIE       0x08    // finished, but TCB still reclaimed

extern task_t * task_create (int priority, void * proc,
                             void * a1, void * a2, void * a3, void * a4);
extern void     task_exit   ();
extern void     task_suspend();
extern void     task_resume (task_t * tid);
extern void     task_delay  (int ticks);
extern void     task_wakeup (task_t * tid);

#endif // CORE_TASK_H
