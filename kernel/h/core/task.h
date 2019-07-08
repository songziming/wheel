#ifndef CORE_TASK_H
#define CORE_TASK_H

#include <base.h>
#include <arch.h>

#include <core/spin.h>

typedef struct task {
    regs_t      regs;   // arch-specific status
    spin_t      lock;
} task_t;

#endif // CORE_TASK_H
