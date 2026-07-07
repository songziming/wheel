#ifndef KOBJ_H
#define KOBJ_H

#include <spinlock.h>
#include <pool_slub.h>
#include <dllist.h>
#include <task.h>

typedef struct kobj kobj_t;
typedef void (*kobj_dtor_t)(void *obj);

// 内核对象类，管理相同类型的对象
// 如果需要定义新的内核对象类型，只需定义一个 kclass
// 其实 kclass 也属于特殊的 kobj，但不是动态创建的
typedef struct kclass {
    const char *name;
    kobj_dtor_t dtor;    // 析构函数
    dlnode_t    clsnode; // guarded by g_classes_lock
    spinlock_t  lock;
    dlnode_t    head;   // guarded by lock
    pool_t      pool;   // guarded by lock
} kclass_t;

// 代表一个内核对象，自动引用计数，同步控制
// 类似 linux kernel kref
typedef struct kobj {
    dlnode_t    objnode; // protected by kclass->lock
    const char *name;
    _Atomic int refcnt;
    // spinlock_t  lock;   // 也用来保护子类字段
    // prioq_t     waitq;  // 等待对象删除，guarded by lock
} kobj_t;

void kclass_register(kclass_t *cls, const char *name, size_t objsize, kobj_dtor_t dtor);
kobj_t *kobj_make(kclass_t *cls, const char *name);
kobj_t *kobj_find(kclass_t *cls, const char *name);
kobj_t *kobj_keep(kobj_t *obj);                 // 引用数 +1
void    kobj_drop(kclass_t *cls, kobj_t *obj);  // 引用数 -1
void    kobj_free(kclass_t *cls, kobj_t *obj);  // 释放对象，在析构函数里面调用，可以 defer
// int     kobj_join(kclass_t *cls, kobj_t *obj, int timeout);

#endif // KOBJ_H
