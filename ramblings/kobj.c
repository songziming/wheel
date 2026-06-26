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


// 管理所有的类
static spinlock_t g_classes_lock = SPINLOCK_INIT;
static dlnode_t g_all_classes;


// 内核对象类，管理相同类型的对象
// 如果需要定义新的内核对象类型，只需定义一个 kclass
// 其实 kclass 也属于特殊的 kobj，但不是动态创建的
typedef struct kclass {
    const char *name;
    dlnode_t    clsnode; // guarded by g_classes_lock
    spinlock_t  lock;
    dlnode_t    head;   // guarded by lock
    pool_t      pool;   // guarded by lock
} kclass_t;


INIT_TEXT void kobj_manager_init() {
    dl_init_circular(&g_all_classes);
}

void kclass_defined(kclass_t *cls, const char *name, size_t objsize) {
    cls->name = name;
    cls->lock = SPINLOCK_INIT;
    dl_init_circular(&cls->head);
    pool_init(&cls->pool, objsize);
    {
        SPINLOCK_SCOPED(&g_classes_lock);
        dl_insert_before(&cls->clsnode, &g_all_classes);
    }
}


typedef struct kobj {
    dlnode_t    objnode; // protected by kclass->lock
    const char *name;
    _Atomic int refcnt;
    spinlock_t  lock;
    prioq_t     waitq;  // guarded by lock
} kobj_t;



// TODO 需要检查 name 是否与现有对象重复
kobj_t *kobj_create(kclass_t *cls, const char *name) {
    {
        SPINLOCK_SCOPED(&g_classes_lock);
        ASSERT(dl_contains(&g_all_classes, &cls->clsnode));
    }

    kobj_t *obj = NULL;
    {
        SPINLOCK_SCOPED(&cls->lock);
        obj = (kobj_t*)pool_alloc_nolock(&cls->pool);
        if (NULL == obj) {
            logk("cannot allocate %s:%s\n", cls->name, name);
            return NULL;
        }
        dl_insert_before(&obj->objnode, &cls->head);
    }

    obj->lock = SPINLOCK_INIT;
    obj->refcnt = 1;
    prioq_init(&obj->waitq);
    obj->name = name;
    return obj;
}


kobj_t *kobj_keep(kobj_t *obj) {
    SPINLOCK_SCOPED(&obj->lock);
    obj->refcnt++;
    return obj;
}

kobj_t *kobj_find(kclass_t *cls, const char *name) {
    SPINLOCK_SCOPED(&cls->lock);

    for (dlnode_t *i = cls->head.next; i != &cls->head; i = i->next) {
        kobj_t *obj = containerof(i, kobj_t, objnode);
        if (0 == kstrcmp(name, obj->name)) {
            ++obj->refcnt;
            return obj;
        }
    }

    return NULL;
}


// 只有一个owner访问对象才能调用此函数
static void kobj_release_nolock(kclass_t *cls, kobj_t *obj) {
    // 唤醒所有等待此对象释放的线程
    while (task_unpend_one(&obj->waitq)) {}

    // 释放对象
    {
        SPINLOCK_SCOPED(&cls->lock);
        dl_remove(&obj->objnode);
        pool_free_nolock(&cls->pool, obj);
    }
}


// 释放对象
void kobj_drop(kclass_t *cls, kobj_t *obj) {
    {
        SPINLOCK_SCOPED(&obj->lock);
        if (--obj->refcnt > 0) {
            return; // 还不能释放
        }

        // 其他线程可能在等待，将它们唤醒
        // 被唤醒的线程还需要取消 wdog
        while (task_unpend_one(&obj->waitq)) {}
    }

    // TODO 以将执行 task_join 的线程唤醒，这些线程唤醒之后会执行 wdog_cancel，取消超时定时器
    //      但是在 wdog_cancel 执行之前，wdog 可能已经超时，可能执行 join_timeout 函数
    //      超时函数里面，需要访问 obj，将任务从 obj->waitq 取出
    //  也就是，此时此刻，另一个CPU可能也在访问这个obj，存在 use-after-free 的风险
    // TODO 最好在 unpend_all 之前，首先把所有的 wdog 清除，这就要求我们将 wdog 也记录在 waitq 里面

    // 运行到这里，引用数为零，说明只有我们在访问这个对象
    // 等待删除的线程也已经恢复，没有其他代码使用这个对象
    kobj_release_nolock(cls, obj);
}


// TODO 有一种可能，超时 ISR 触发的时候，目标对象恰好被删除了
// cancel-wdog 之后，timeout ISR 还可能触发，也就是本函数执行时可能对象已经释放
static void join_timeout(wdog_t *wd) {
    waiter_t *waiter = containerof(wd, waiter_t, timer);
    kobj_t *obj = (kobj_t*)waiter->user; // 可能是野指针
    SPINLOCK_SCOPED(&obj->lock);
    task_wake_timeout(&obj->waitq, waiter);
}


// 释放对象，并等待这个对象被删除
// 这个函数可以用于 task_join
// 等到了对象释放则返回 1，超时退出则返回 0
int kobj_join(kclass_t *cls, kobj_t *obj, int timeout) {
    int remain;
    waiter_t waiter;

    {
        SPINLOCK_SCOPED(&obj->lock);
        remain = --obj->refcnt;
        if (remain != 0) {
            // 不能立即释放，将自己放入阻塞队列
            waiter.user = obj;
            task_pend(&obj->waitq, &waiter, timeout, join_timeout);
        } else {
            // 需要删除对象，将等待的线程唤醒（持有锁）
            // 被唤醒的线程可能执行 wdog timeout ISR
            while (task_unpend_one(&obj->waitq)) {}
        }
    }

    if (0 == remain) {
        kobj_release_nolock(cls, obj);
        return 1;
    }

    arch_task_switch();

    // 恢复运行，检查是否因为超时而唤醒
    // 从这里开始，不能再访问 obj，可能已经是野指针了
    task_onresume(&waiter);
    return waiter.got;
}
