
// 调度对象，调度器视角下操作的对象
// 这是内核线程所需的最小信息
typedef struct sched_item {
    size_t      stack_top;  // regs_t
    size_t      stack0;     // stack_top when syscall
    size_t      stack3;     // saved by syscall
    dlnode_t    dl;         // node in ready-queue|wait-queue
    _Atomic uint32_t state;
    int16_t     affinity;
    int16_t     priority;

    // 下面的字段和阻塞有关
    wdog_t      timer;      // 超时定时器，触发后调用 task_timeout
    spinlock_t *wait_lock;  // 该 waitq 的锁（给 timeout callback 用），确认 wdog 删除之后再清除
    prioq_t    *wait_wq;    // 所在的阻塞队列，不在阻塞队列则取值 NULL（guarded by wait_lock）
    int         got;        // 是否被正常唤醒（非超时）
    int         expired;    // 是否因超时被唤醒（TODO 未使用）
} sched_item_t;

typedef struct task {
    kobj_t      obj;
    sched_item_t sched;

    size_t      pgtbl;      // 就是 process->vm.table，放在这里便于访问
    process_t  *process;    // parent process (NULL if kernel thread)
    vmrange_t   user_stack; // user stack
} task_t;