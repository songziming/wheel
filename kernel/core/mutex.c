#include "mutex.h"
#include <kobj.h>
#include <task.h>
#include <spinlock.h>
#include <debug.h>

// 二值的信号量，但是更严格，必须由相同的线程获取/释放
// 不能跨线程获取/释放，也不能在 ISR 里面使用

static kclass_t g_mutex_class;

typedef struct mutex {
    spinlock_t  lock;
    prioq_t     wq;
    task_t     *owner;  // guarded by lock, NULL if not locked
} mutex_t;


INIT_TEXT void mutex_init(void) {
    kclass_register(&g_mutex_class, "mutex", sizeof(mutex_t), NULL);
}

mutex_t *mutex_make(const char *name) {
    mutex_t *mut = (mutex_t*)kobj_make(&g_mutex_class, name);
    if (NULL == mut) {
        return NULL;
    }
    mut->lock = SPINLOCK_INIT;
    prioq_init(&mut->wq);
    mut->owner = NULL;
    return mut;
}

// 返回 1 表示成功得到锁
// 返回 0 表示未得到锁，超时
int mutex_take(mutex_t *mut, int timeout) {
    ASSERT(0 == cpu_int_depth());

    task_t *self = current_task();

    {
        SPINLOCK_SCOPED(&mut->lock);
        ASSERT(self != mut->owner); // 不许重入
        if (NULL == mut->owner) {
            mut->owner = self;
            return 1;
        }
        if (NOWAIT == timeout) {
            return 0;
        }
        // 没有得到，阻塞当前任务
        task_pend(&mut->wq, &mut->lock, timeout);
    }

    arch_task_switch();
    return self->got;
}

void mutex_give(mutex_t *mut) {
    ASSERT(0 == cpu_int_depth());
    task_t *tid;
    {
        // 锁内原子地 claim 下一个等待者并交接 owner
        SPINLOCK_SCOPED(&mut->lock);
        task_t *self = current_task();
        if (self != mut->owner) {
            panic("release mutex from %p, owner=%p\n", self, mut->owner);
        }
        tid = task_unpend_one_nolock(&mut->wq);
        mut->owner = tid; // NULL 表示无人等待
    }
    // 锁外唤醒
    if (tid) {
        task_unpend_finish(tid);
        arch_task_switch();
    }
}

void mutex_drop(mutex_t *mut) {
    kobj_drop(&g_mutex_class, mut);
}
