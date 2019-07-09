#ifndef CORE_TASK_H
#define CORE_TASK_H

#include <base.h>
#include <arch.h>

#include <core/spin.h>

typedef struct task {
    regs_t      regs;   // arch-specific status
    spin_t      lock;

    u32         state;
    int         priority;
    cpuset_t    affinity;
} task_t;

// task priorities
#define PRIORITY_COUNT  32      // we use `u32` as priority bitmask
#define PRIORITY_IDLE   31      // lowest priority = idle
#define PRIORITY_NONRT  30      // 2nd lowest priority = non-real-time



#endif // CORE_TASK_H
