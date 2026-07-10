#ifndef KOBJ_H
#define KOBJ_H

#include <spinlock.h>
#include <pool_slub.h>
#include <dllist.h>

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

void kclass_register(kclass_t *cls, const char *name, size_t objsize, kobj_dtor_t dtor);

const char *kobj_name(const void *ptr);

void *kobj_make(kclass_t *cls, const char *name);
void *kobj_find(kclass_t *cls, const char *name);
void *kobj_keep(void *obj);                 // 引用数 +1
void  kobj_drop(kclass_t *cls, void *obj);  // 引用数 -1，可能执行析构函数
void  kobj_free(kclass_t *cls, void *obj);  // 不析构，直接释放，用于构造失败的清空

#endif // KOBJ_H
