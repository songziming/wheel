#include "lockdep.h"

#if defined(LOCKDEP)

#include <task.h>
#include <arch_api.h>
#include <debug.h>

static CONST int g_lockdep_ready;

INIT_TEXT void lockdep_enable() { g_lockdep_ready = 1; }

// TODO 打印函数也会用到锁，如果发现锁异常，应该直接让 bochs 中断
void lockdep_acquire(void *lock, lockdep_instance_t *dep) {
    if (!g_lockdep_ready) { return; }
    if (NULL == dep->file) { return; }

    task_t *task = THISCPU_GET(g_prev_task);
    if (NULL == task) { return; }

    for (int i = 0; i < task->lockdep.depth; i++) {
        if (task->lockdep.held[i].lock == lock) {
            panic("LOCKDEP: %s tried to acquire %s:%d which it already holds",
                  task->name, dep->file, dep->line);
        }
    }

    if (task->lockdep.depth >= LOCKDEP_MAX_HELD) {
        panic("LOCKDEP: %s held-lock stack overflow at %s:%d (depth=%d)",
              task->name, dep->file, dep->line, task->lockdep.depth);
    }

    task->lockdep.held[task->lockdep.depth].lock = lock;
    task->lockdep.held[task->lockdep.depth].dep  = dep;
    task->lockdep.depth++;
}

void lockdep_release(void *lock, lockdep_instance_t *dep) {
    if (!g_lockdep_ready) { return; }
    if (NULL == dep->file) { return; }

    task_t *task = THISCPU_GET(g_prev_task);
    if (NULL == task) { return; }

    if (0 == task->lockdep.depth) {
        panic("LOCKDEP: %s released %s:%d but held no locks",
              task->name, dep->file, dep->line);
    }

    lockdep_held_t *top = &task->lockdep.held[task->lockdep.depth - 1];
    if (top->lock != lock) {
        panic("LOCKDEP: %s released %s:%d out of order (expected %s:%d)",
              task->name, dep->file, dep->line,
              top->dep->file ? top->dep->file : "?",
              top->dep->line);
    }

    task->lockdep.depth--;
}

#endif // LOCKDEP
