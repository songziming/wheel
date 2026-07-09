// 内核对象机制
// 可以动态创建，引用计数，自动删除，同步控制
// 一个对象可以有多个持有者，refcnt 就是 owner 数量

// 内核对象可以用于 TCB、PCB、endpoint
// 服务程序可以注册 endpoint，调用进程使用对象名称寻找这个 endpoint


#include "kobj.h"
#include <kstring.h>
#include <debug.h>

#include <kshell.h>
#include <console.h>


// 代表一个内核对象，自动引用计数，同步控制
// 类似 linux kernel kref
typedef struct kobj {
    dlnode_t    objnode; // protected by kclass->lock
    const char *name;
    _Atomic int refcnt;
    uint32_t    padding;
    size_t      payload[0];
} kobj_t;


// 管理所有的对象类
static spinlock_t g_classes_lock = SPINLOCK_INIT;
static DEFINE_DL_HEAD(g_all_classes);


void kclass_register(kclass_t *cls, const char *name, size_t objsize, kobj_dtor_t dtor) {
    cls->name = name;
    cls->dtor = dtor;
    cls->lock = SPINLOCK_INIT;
    dl_init_circular(&cls->head);
    pool_init(&cls->pool, sizeof(kobj_t) + objsize);
    {
        SPINLOCK_SCOPED(&g_classes_lock);
        dl_insert_before(&cls->clsnode, &g_all_classes);
    }
}


const char *kobj_name(const void *ptr) {
    const kobj_t *obj = (const kobj_t*)((const char*)ptr - sizeof(kobj_t));
    return obj->name;
}


void *kobj_make(kclass_t *cls, const char *name) {
#ifdef DEBUG
    {
        SPINLOCK_SCOPED(&g_classes_lock);
        ASSERT(dl_contains(&g_all_classes, &cls->clsnode));
    }
#endif
    kobj_t *obj = NULL;
    {
        SPINLOCK_SCOPED(&cls->lock);
        obj = (kobj_t*)pool_alloc_nolock(&cls->pool);
        if (NULL == obj) {
            logk("cannot allocate %s:%s\n", cls->name, name);
            return NULL;
        }
        obj->name = name; // TODO 需要检查 name 是否与现有对象重复
        obj->refcnt = 1;
        dl_insert_before(&obj->objnode, &cls->head);
    }
    return &obj->payload;
}

// 可能搜索到一个引用数为零的对象，即将删除还残留在队列中的对象
// 需要持有 class-lock 同时判断对象的 refcnt 大于零
void *kobj_find(kclass_t *cls, const char *name) {
    SPINLOCK_SCOPED(&cls->lock);

    for (dlnode_t *i = cls->head.next; i != &cls->head; i = i->next) {
        kobj_t *obj = containerof(i, kobj_t, objnode);
        if (kstrcmp(name, obj->name)) {
            continue;
        }

        int old = atomic_fetch_add(&obj->refcnt, 1);
        if (old > 0) {
            return &obj->payload;
        }

        // old-refcnt==0，说明对象已经释放，但仍残留在队列中等待释放
        // 需要将其恢复为 0，防止再次 kobj_find（此时持有 class-lock，是安全的）
        atomic_store(&obj->refcnt, 0);
        return NULL;
    }

    return NULL;
}

void *kobj_keep(void *ptr) {
    kobj_t *obj = (kobj_t*)((char*)ptr - sizeof(kobj_t));
    int old = atomic_fetch_add(&obj->refcnt, 1);
    ASSERT(old > 0);
    (void)old;
    return ptr;
}

// 释放对象
void kobj_drop(kclass_t *cls, void *ptr) {
    kobj_t *obj = (kobj_t*)((char*)ptr - sizeof(kobj_t));
    if (atomic_fetch_sub(&obj->refcnt, 1) > 1) {
        return; // 还不能释放
    }
    if (cls->dtor) {
        cls->dtor(ptr);
    }
    {
        SPINLOCK_SCOPED(&cls->lock);
        dl_remove(&obj->objnode);
        pool_free_nolock(&cls->pool, obj);
    }
}

//------------------------------------------------------------------------------
// 调试命令：查看当前的类和对象
//------------------------------------------------------------------------------

static void show_objs(int argc, char *argv[]) {
    SPINLOCK_SCOPED(&g_classes_lock);

    for (dlnode_t *i = g_all_classes.next; i != &g_all_classes; i = i->next) {
        kclass_t *cls = containerof(i, kclass_t, clsnode);
        if ((argc > 1) && kstrcmp(cls->name, argv[1])) {
            continue;
        }
        console_printf("%s:", cls->name);
        SPINLOCK_SCOPED(&cls->lock);
        for (dlnode_t *j = cls->head.next; j != &cls->head; j = j->next) {
            kobj_t *obj = containerof(j, kobj_t, objnode);
            console_printf(" %s:%d", obj->name, obj->refcnt);
        }
        console_printf(".\n");
    }
}

KSHELL_CMD("obj", show_objs);
