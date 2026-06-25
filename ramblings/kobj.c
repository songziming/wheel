// 内核对象机制
// 可以动态创建，引用计数，自动删除，同步控制
// 一个对象可以有多个持有者，refcnt 就是 owner 数量


#include <spinlock.h>
#include <task.h>
#include <pool_slub.h>


typedef struct kobj {
    spinlock_t  lock;
    int         refcnt; // guarded by lock
    prioq_t     waitq;  // guarded by lock
} kobj_t;


// 内核对象类，管理相同类型的对象
// 如果需要定义新的内核对象类型，只需定义一个 kclass
typedef struct kclass {
    size_t      objsize;
    spinlock_t  lock;
    dlnode_t    head;   // guarded by lock
    pool_t      pool;   // guarded by lock
} kclass_t;




kobj_t *kobj_retain(kobj_t *obj) {
    return obj;
}


// 释放对象
void kobj_release(kobj_t *obj) {
    SPINLOCK_SCOPED(&obj->lock);
    if (0 == --obj->refcnt) {
        // TODO free object to pool
    }
}


static void join_timeout(wdog_t *wd) {
    waiter_t *waiter = containerof(wd, waiter_t, timer);
    kobj_t *obj = (kobj_t*)waiter->user;
    SPINLOCK_SCOPED(&obj->lock);
    task_wake_timeout(&obj->waitq, waiter);
}


// 释放对象，并等待这个对象被删除
// 这个函数可以用于 task_join
void kobj_join(kobj_t *obj, int timeout) {
    SPINLOCK_SCOPED(&obj->lock);
    if (0 == --obj->refcnt) {
        return;
    }

    // 不能立即释放，将自己放入阻塞队列
    waiter_t waiter;
    waiter.user = obj;
    task_pend(&obj->waitq, &waiter, timeout, join_timeout);
}
