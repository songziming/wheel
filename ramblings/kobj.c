// 内核对象机制
// 可以动态创建，引用计数，自动删除，同步控制
// 一个对象可以有多个持有者，refcnt 就是 owner 数量

// 内核对象可以用于 TCB、PCB、endpoint
// 服务程序可以注册 endpoint，调用进程使用对象名称寻找这个 endpoint


#include <spinlock.h>
#include <task.h>
#include <pool_slub.h>
#include <kstring.h>

#include <kshell.h>
#include <console.h>


// 内核对象类，管理相同类型的对象
// 如果需要定义新的内核对象类型，只需定义一个 kclass
typedef struct kclass {
    size_t      objsize;
    spinlock_t  lock;
    dlnode_t    head;   // guarded by lock
    pool_t      pool;   // guarded by lock
} kclass_t;


typedef struct kobj {
    spinlock_t  lock;
    int         refcnt; // guarded by lock
    prioq_t     waitq;  // guarded by lock
    dlnode_t    objnode; // protected by kclass->lock
    const char *name;
} kobj_t;



// TODO 需要检查 name 是否与现有对象重复
kobj_t *kobj_create(kclass_t *cls, const char *name) {
    kobj_t *obj;
    {
        SPINLOCK_SCOPED(&cls->lock);
        obj = (kobj_t*)pool_alloc_nolock(&cls->pool);
        dl_insert_before(&obj->objnode, &cls->head);
    }
    obj->lock = SPINLOCK_INIT;
    obj->refcnt = 1;
    prioq_init(&obj->waitq);
    obj->name = name;
    return obj;
}


kobj_t *kobj_retain(kobj_t *obj) {
    SPINLOCK_SCOPED(&obj->lock);
    obj->refcnt++;
    return obj;
}

kobj_t *kobj_find(kclass_t *cls, const char *name) {
    SPINLOCK_SCOPED(&cls->lock);

    for (dlnode_t *i = cls->head.next; i != &cls->head; i = i->next) {
        kobj_t *obj = containerof(i, kobj_t, objnode);
        if (0 == kstrcmp(name, obj->name)) {
            return obj;
        }
    }

    return NULL;
}


// 释放对象
void kobj_release(kclass_t *cls, kobj_t *obj) {
    {
        SPINLOCK_SCOPED(&obj->lock);
        if (--obj->refcnt > 0) {
            return; // 还不能释放
        }
    }

    // 运行到这里，只有我们在访问这个对象
    // 不持有锁也是安全的

    // 唤醒所有等待此对象释放的线程
    while (task_unpend_one(&obj->waitq)) {}

    // 释放对象
    {
        SPINLOCK_SCOPED(&cls->lock);
        dl_remove(&obj->objnode);
        pool_free_nolock(&cls->pool, obj);
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
// 等到了对象释放则返回1，超时退出则返回 0
int kobj_join(kobj_t *obj, int timeout) {
    waiter_t waiter;

    {
        SPINLOCK_SCOPED(&obj->lock);
        if (0 == --obj->refcnt) {
            return 1;
        }

        // 不能立即释放，将自己放入阻塞队列
        waiter.user = obj;
        task_pend(&obj->waitq, &waiter, timeout, join_timeout);
    }
    arch_task_switch();

    // 恢复运行，检查是否因为超时而唤醒
    task_onresume(&waiter);
    return waiter.got;
}
