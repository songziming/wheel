#include "lockdep.h"

#if defined(LOCKDEP)

#include <task.h>
#include <arch_api.h>
#include <debug.h>


// lockdep 限制了 take/give 必须成对出现
// 然而锁还可以用于线程同步，生产者只有 give，消费者只有 take，lockdep 会报错

// lockdep 需要使用 thiscpu，不能开机就生效
// lockdep 还需要 tid_prev，为了尽快生效，需要让 tid_prev 指向有效的 TCB
static CONST int g_lockdep_ready = 0;
INIT_TEXT void lockdep_enable() {
    g_lockdep_ready = 1;
}

void lockdep_task_init(lockdep_task_t *task) {
    task->depth = 0;
}

// 得到锁之后执行
void lockdep_acquire(void *lock, lockdep_instance_t *dep) {
    if (!g_lockdep_ready) { return; }
    if (NULL == dep->file) { return; }

    task_t *task = current_task();
    if (NULL == task) { return; }

    // 搜索当前持有的锁，检查重复 lock
    for (int i = 0; i < task->lockdep.depth; i++) {
        if (task->lockdep.held[i].lock == lock) {
            panic("LOCKDEP: %s tried to acquire %s:%d which it already holds",
                  task->name, dep->file, dep->line);
        }
    }

    // 持有的锁太多
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

    task_t *task = current_task();
    if (NULL == task) { return; }

    if (0 == task->lockdep.depth) {
        panic("LOCKDEP: %s released %s:%d but held no locks",
              task->name, dep->file, dep->line);
    }

    // 释放的锁必须是最后一个获取的，不能违反顺序
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
