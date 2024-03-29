# 内核对象与句柄

- 内核对象：kobj
- 句柄：handle

凡是动态分配的内核空间的 object，就是内核对象。task_t、blk_dev_t、semaphore_t，这些都是内核对象。

内核对象可能被随时创建，随时删除。一个任务持有某个 kobj 的指针，可能该对象就被其他任务删除了，该任务全然不知。

指针变成野指针，显然要通过流程设计避免这种情况。但也可以通过 handle 解决。

指针是单向的，任务 T1 持有对象 A1 的指针，但 A1 不知道自己正在被 T1 引用。如果此时 A1 删除了，无法告知 T1。

句柄是双向的，有点类似于链表节点。任务 T1 持有 A1 的句柄，A1 也知道引用自己的句柄有哪些。

如果要删除对象 A1，可以遍历引用自己的句柄（遍历链表），将每个句柄标记为失效（invalid）。


有点类似 C++ 自动指针？智能指针？

* * *

# 自动引用计数、自旋锁保护

~~~c
typedef struct kobj {
    spin_t  spin;
    int     objtype;
    int     refcnt;
    void   *underlying;
} kobj_t;
~~~

内核内部代码，也不直接使用原始指针，而是用 kobj 封装一层。

kobj->underlying 指向真正的目标对象，如 blk_dev_t、task_t。

必须通过 kobj 间接访问，因为 kobj 提供了自旋锁保护、自动引用计数，而且能处理目标 object 被强行删除的情况。

几个工具函数：

- kobj_incref，表示增加一个对 kobj 的引用（原子性操作）
- kobj_decref，减少一个对 kobj 的引用（原子性操作，当引用数为零，自动删除 kobj）
- kobj_lock，锁住 kobj（以及引用的目标对象），接下来可以对 underlying 做一些复杂的操作
- kobj_release，释放锁，允许其他 kobj 持有者操作

相比于持有裸指针，kobj 不用担心目标对象被删除，持有的指针变成野指针。

由于底层对象只有一个直接引用，即 underlying 字段，只要把 underlying 设为 NULL，后面的访问者就会自然发现。

## 应用情景分析

kobj 解决的最核心需求，就是多个引用持有者的情况下，对象被强行删除。对引用持有者而言，对象在自己不知情的情况下被删除。

这种情况真的会出现吗？能否通过流程避免这种情况的出现？例如首先让持有引用的 task 全部退出，再删除对象？

1. 可移动存储介质可能被用户拔出，这时块设备应该立即失效。引用这个 block_dev 的任务显然无法得到通知。
不过，完全可以通过 flags 标记为已失效，不必立即删除。

2. 持有另一个任务的指针，目标任务可能自己退出，导致 task_t 被释放。
不过，持有其他任务 TCB 的指针，说明准备控制其他任务的执行状态？类似于 controller 和 worker，显然应该由开发者保证子任务不能随意退出。
如果只是想任务间通信，应该使用 semaphore、pipe 这类机制。一般只有 OS 调度模块能直接操控 TCB。

由此来看，kobj 可能是伪需求。
