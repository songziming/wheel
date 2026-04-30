#include "lockdep.h"

#if defined(LOCKDEP)

#include <task.h>
#include <arch_api.h>
#include <debug.h>


// lockdep 限制了 take/give 必须成对出现
// 然而锁还可以用于线程同步，生产者只有 give，消费者只有 take，lockdep 会报错

// TODO 使用 panic 报错有风险
// 打印函数也会用到锁，如果发现锁异常，应该直接让 bochs 中断


static CONST int g_lockdep_ready;

// 得到锁之后执行
INIT_TEXT void lockdep_enable() { g_lockdep_ready = 1; }
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

// 释放锁之前执行
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
