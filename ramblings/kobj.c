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
    // task_unpend_one 每次 claim 自带 obj->lock，wdog_cancel 在锁外等回调跑完
    // 循环结束意味着所有 wdog 已 cancel，回调不会再触发，obj 可安全释放
    while (task_unpend_one(&obj->waitq, &obj->lock)) {}

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
    }

    // 引用数为零，唤醒等待者并释放
    // 不再需要在这里先唤醒一次：kobj_release_nolock 会做，且新协议下
    // task_unpend_one 自带锁 + 锁外 wdog_cancel，不再有"持锁调 wdog_cancel"的死锁
    kobj_release_nolock(cls, obj);
}


// 释放对象，并等待这个对象被删除
// 这个函数可以用于 task_join
// 等到了对象释放则返回 1，超时退出则返回 0
int kobj_join(kclass_t *cls, kobj_t *obj, int timeout) {
    task_t *self = current_task();
    int remain;

    {
        SPINLOCK_SCOPED(&obj->lock);
        remain = --obj->refcnt;
        if (remain != 0) {
            // 不能立即释放，将自己放入阻塞队列
            task_pend(&obj->waitq, &obj->lock, timeout);
        }
    }

    if (0 == remain) {
        // 引用数为零，唤醒所有等待者并释放对象
        kobj_release_nolock(cls, obj);
        return 1;
    }

    arch_task_switch();
    return self->got;
}
