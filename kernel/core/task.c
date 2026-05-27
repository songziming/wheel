// 任务管理
// 任务的创建/删除/状态转换
// 本模块的函数服务于 semaphore、mutex、msgq 这类同步原语
// 通常不需要直接调用这些函数


#include "task.h"
#include "work.h"
#include <arch_api.h>
#include <spin.h>
#include <dllist.h>
#include <kstring.h>
#include <heap.h>
#include <debug.h>


static INIT_BSS task_t g_dummy_tcb = {.lockdep=LOCKDEP_TASK_INIT};
static PERCPU_BSS task_t g_idle_tcb;
static NORETURN void task_entry(void (*real)());
static NORETURN void proc_idle();

// ready queue
static PERCPU_DATA spin_t g_rdy_lock = SPIN_INIT;
static PERCPU_BSS prioq_t g_rdyq;

// task switch
PERCPU_DATA task_t *g_tid_prev = &g_dummy_tcb;  // updated in int_return
PERCPU_DATA task_t *g_tid_next = NULL;          // guarded by g_rdyq.lock
PERCPU_DATA int g_preempt_depth = 0;

// 负载均衡
static _Atomic uint64_t g_idle_mask = 0UL;
static _Atomic uint32_t g_next_cpu = 0U;


//------------------------------------------------------------------------------
// 就绪队列函数，全部无锁
//------------------------------------------------------------------------------

void prioq_init(prioq_t *q) {
    q->priorities = 0U;
    kmemset(q->heads, 0, sizeof(q->heads));
}

static void prioq_insert(prioq_t *q, dlnode_t *dl, int prio) {
    if (NULL == q->heads[prio]) {
        dl_init_circular(dl);
        q->heads[prio] = dl;
        q->priorities |= 1U << prio;
    } else {
        dl_insert_before(dl, q->heads[prio]);
    }
}

static void prioq_remove(prioq_t *q, dlnode_t *dl, int prio) {
    ASSERT(q->priorities & (1U << prio));
    ASSERT(NULL != q->heads[prio]);

    if (dl_is_lastone(dl)) {
        ASSERT(q->heads[prio] == dl);
        q->heads[prio] = NULL;
        q->priorities &= ~(1U << prio);
    } else {
        if (dl == q->heads[prio]) {
            q->heads[prio] = dl->next;
        } else {
            ASSERT(dl_contains(q->heads[prio], dl));
        }
        dl_remove(dl);
    }
}

static int prioq_contains(prioq_t *q, dlnode_t *dl, int prio) {
    if (q->heads[prio]) {
        return (dl==q->heads[prio]) || dl_contains(q->heads[prio], dl);
    }
    return 0;
}

static dlnode_t *prioq_head(prioq_t *q) {
    int prio = __builtin_ctz(q->priorities);
    return q->heads[prio];
}

//------------------------------------------------------------------------------
// 禁用抢占但允许中断
//------------------------------------------------------------------------------

inline void preempt_lock() {
    THISCPU_ADD(g_preempt_depth, 1);
}

// 解除抢占锁，但不会立即切换任务，需要调用 arch_task_switch 才能触发
inline void preempt_unlock() {
    THISCPU_ADD(g_preempt_depth, -1);
}

// 获得当前任务，无需获得 rdyq-lock
// 因为 tid_prev 只会在中断返回时更新，在任务里读取这个值就是正确的
inline task_t *current_task() {
    return THISCPU_GET(g_tid_prev);
}

//------------------------------------------------------------------------------
// 任务调度
//------------------------------------------------------------------------------

// 初始化调度器
INIT_TEXT void sched_init() {
    prioq_t *q = THISCPU(&g_rdyq);
    prioq_init(q);

    int cpu = cpu_index();

    // 创建 idle-task
    task_t *idle = THISCPU(&g_idle_tcb);
    task_create(idle, kernel_heap_mkstr("idle-%d", cpu), 31, proc_idle);
    idle->affinity = cpu;
    idle->state = TS_READY;
    prioq_insert(q, &idle->dl, 31);
    g_idle_mask |= 1UL << cpu;

    g_dummy_tcb.priority = 33; // 确保能被抢占
    g_dummy_tcb.lockdep = LOCKDEP_TASK_INIT;
    THISCPU_SET(g_tid_prev, &g_dummy_tcb);
    THISCPU_SET(g_tid_next, idle);
}

// called in timer ISR
// 只负责轮转，不抢占（抢占通过 arch_task_switch 触发）
void sched_process() {
    ASSERT(cpu_int_depth() > 0);
    spin_t *lock = THISCPU(&g_rdy_lock);
    raw_spin_take(lock);

    task_t *prev = THISCPU_GET(g_tid_next);
    task_t *next = containerof(prev->dl.next, task_t, dl);
    THISCPU_SET(g_tid_next, next);

    raw_spin_give(lock);
}

//------------------------------------------------------------------------------
// 创建任务，处于 STOPPED 状态，需要使用 task_start 启动
//------------------------------------------------------------------------------

void task_create(task_t *tid, const char *name, int prio, void *func) {
    tid->state    = TS_STOPPED;
    tid->name     = name;
    tid->priority = prio;
    tid->affinity = -1;
    tid->lockdep  = LOCKDEP_TASK_INIT;

    vmspace_alloc_stack(&g_kernel_vm, &tid->stack, 0);
    tid->stack.desc = name;
    // logk("alloc stack for %s at 0x%zx\n", name, tid->stack.vaddr);
    arch_task_init(tid, (size_t)task_entry, tid->stack.vend, (size_t)func,0,0,0);
}

void task_create_ex(task_t *tid, const char *name, int prio, void *func, uint32_t stack_rank) {
    tid->state    = TS_STOPPED;
    tid->name     = name;
    tid->priority = prio;
    tid->affinity = -1;

    vmspace_alloc_stack(&g_kernel_vm, &tid->stack, stack_rank);
    tid->stack.desc = name;
    arch_task_init(tid, (size_t)task_entry, tid->stack.vend, (size_t)func,0,0,0);
}

//------------------------------------------------------------------------------
// 任务停止运行，只能停止当前任务
//------------------------------------------------------------------------------

// 调用者需要持有 waitq 所在对象的锁，中断关闭
// 超时回调函数需要锁住同步对象，我们不知道同步对象是什么
void task_pend(prioq_t *wq, waiter_t *pender, int timeout, wdog_cb_t cb) {
    ASSERT(0 == cpu_int_depth());

    task_t *self = THISCPU_GET(g_tid_prev);
    prioq_t *q = THISCPU(&g_rdyq);
    spin_t *lock = THISCPU(&g_rdy_lock);

    self->state |= TS_PENDING;

    raw_spin_take(lock);
    prioq_remove(q, &self->dl, self->priority);
    task_t *next = containerof(prioq_head(q), task_t, dl);
    if (31 == next->priority) {
        atomic_fetch_or(&g_idle_mask, 1UL << cpu_index());
    }
    THISCPU_SET(g_tid_next, next);
    raw_spin_give(lock);

    pender->expired = 0;
    pender->tid = self;

    prioq_insert(wq, &pender->dl, self->priority);
    if (FOREVER != timeout) {
        wdog_start(&pender->timer, cb, timeout);
    }
}

//------------------------------------------------------------------------------
// 恢复另一个任务
//------------------------------------------------------------------------------

static void _cont_this(task_t *tid) {
    spin_t *lock = THISCPU(&g_rdy_lock);
    prioq_t *q = THISCPU(&g_rdyq);
    int key = irq_spin_take(lock);

    prioq_insert(q, &tid->dl, tid->priority);
    if (tid->priority < THISCPU_GET(g_tid_next)->priority) {
        THISCPU_SET(g_tid_next, tid);
    }
    atomic_fetch_and(&g_idle_mask, ~(1UL << cpu_index()));

    irq_spin_give(lock, key);
}

static void _cont_cpu(task_t *tid, int cpu) {
    spin_t *lock = THISCPU(&g_rdy_lock);
    prioq_t *q = PERCPU(cpu, &g_rdyq);
    int key = irq_spin_take(lock);

    prioq_insert(q, &tid->dl, tid->priority);
    if (tid->priority < (*PERCPU(cpu, &g_tid_next))->priority) {
        *PERCPU(cpu, &g_tid_next) = tid;
    }
    atomic_fetch_and(&g_idle_mask, ~(1UL << cpu));

    irq_spin_give(lock, key);
}

// 恢复运行这个 task，返回需要切换任务的 CPU-mask
// 启动任务、semaphore/fence 恢复任务时调用此函数
// 如果任务恢复运行，返回目标 cpu 的编号，未运行则返回 -1
static int task_cont(task_t *tid, uint32_t bits) {
    ASSERT(TS_READY != tid->state);

    tid->state &= ~bits;
    if (TS_READY != tid->state) {
        return -1; // still no ready
    }

    // 变为运行态，寻找一个 CPU 来运行
    int cpu = tid->affinity;
    if (cpu < 0) {
        uint64_t this_mask = 1ULL << cpu_index();
        uint64_t idle_mask = atomic_load(&g_idle_mask);
        if (idle_mask & this_mask) {
            cpu = cpu_index(); // prefer thiscpu
        } else if (0 != idle_mask) {
            cpu = __builtin_ctzll(idle_mask);
            ASSERT(cpu_index() != cpu);
        } else {
            // 按顺序挑选，平摊负载
            cpu = atomic_fetch_add(&g_next_cpu, 1) % cpu_count();
        }
    }

    if (cpu_index() == cpu) {
        _cont_this(tid);
    } else {
        _cont_cpu(tid, cpu);
    }

    // 返回 cpu-mask，这样批量恢复任务时（如 semaphore、fence）
    // 就可以最后统一发送 IPI
    return cpu;
}

// 任务获取到资源，按正常流程唤醒下一个等待的任务
// 由上一个owner调用，可能在 ISR 里面调用
// 返回1表示启动了一个任务，返回0表示没有阻塞的线程
task_t *task_unpend_one(prioq_t *wq) {
    dlnode_t *dl = prioq_head(wq);
    if (NULL == dl) {
        return NULL;
    }

    // 存在阻塞者，将其唤醒，并将其设为新的 owner
    waiter_t *w = containerof(dl, waiter_t, dl);
    w->got = 1;
    prioq_remove(wq, dl, w->tid->priority);
    int cpu = task_cont(w->tid, TS_PENDING);
    if (cpu_index() != cpu) {
        arch_send_ipi(cpu, VEC_IPI_RESCHED);
    }
    return w->tid;
}

// 在超时回调ISR里执行
// caller 需要锁住同步对象
void task_wake_timeout(prioq_t *wq, waiter_t *pender) {
    ASSERT(0 != cpu_int_depth());

    task_t *tid = pender->tid;
    if (!prioq_contains(wq, &pender->dl, tid->priority)) {
        // 已经移除了阻塞队列，可能是timer触发之后任务被正常恢复
        return;
    }

    pender->expired = 1; // 标记 pend 状态已超时
    prioq_remove(wq, &pender->dl, tid->priority);
    int cpu = task_cont(tid, TS_PENDING);
    if ((cpu >= 0) && (cpu_index() != cpu)) {
        arch_send_ipi(cpu, VEC_IPI_RESCHED);
    }
}

// 从阻塞状态恢复，需要删除 wdog
// pender 通常位于函数栈，任务恢复之后 pender 即将析构，wdog 也即将析构
// 未注册 wdog 也可以删除，不会出错
void task_onresume(waiter_t *pender) {
    ASSERT(0 == cpu_int_depth());
    wdog_cancel(&pender->timer);
}

//------------------------------------------------------------------------------
// 退出自身任务
//------------------------------------------------------------------------------

typedef struct freework {
    work_t wk;
    task_t *tid;
} freework_t;

// work 函数还要访问 next
// work 也在函数栈上，删除映射后，不能再访问 work
// 所以提前读出 task，后面直接用 tid
static void task_free(work_t *wk) {
    freework_t *work = containerof(wk, freework_t, wk);
    task_t *tid = work->tid;
    vmspace_remove(&g_kernel_vm, &tid->stack);
    tid->state = TS_DELETED;
}

// 全程必须关闭中断，防止被抢占，否则 work 没来得及注册，任务无法回收
// 大致逻辑与 task_stop 类似
void task_exit() {
    ASSERT(cpu_int_depth() == 0);
    ASSERT(THISCPU_GET(g_preempt_depth) == 0);

    task_t *self = THISCPU_GET(g_tid_prev);
    prioq_t *q = THISCPU(&g_rdyq);
    spin_t *lock = THISCPU(&g_rdy_lock);

    // 发送 IPI，让其他 cpu 清除此任务的栈
    // 这样只剩当前 cpu 还保留 mapping，留到 work 里面删除
    tlb_shootdown(self->stack.vaddr, self->stack.vend);

    self->state |= TS_STOPPED;

    int key = irq_spin_take(lock);
    prioq_remove(q, &self->dl, self->priority);
    task_t *next = containerof(prioq_head(q), task_t, dl);
    if (31 == next->priority) {
        atomic_fetch_or(&g_idle_mask, 1UL << cpu_index());
    }
    THISCPU_SET(g_tid_next, next);

    // 注册 work，此时中断关闭，确保 work 不会触发
    freework_t freework;
    freework.tid = self;
    work_defer(&freework.wk, task_free, "freetask");

    irq_spin_give(lock, key);
    arch_task_switch();
}


//------------------------------------------------------------------------------

// 启动任务并立即切换
// 适合只启动一个任务，无需禁用抢占，启动之后立即可切换
void task_start_now(task_t *tid) {
    int cpu = task_cont(tid, TS_STOPPED);
    if (cpu_index() == cpu) {
        arch_task_switch();
    } else if (cpu >= 0) {
        arch_send_ipi(cpu, VEC_IPI_RESCHED);
    }
}

// 启动任务但暂时不要切换
// 如果批量启动任务，顺序是：禁用抢占，启动任务，发送 ipi，启用抢占，切换
uint64_t task_start(task_t *tid) {
    int cpu = task_cont(tid, TS_STOPPED);
    return (cpu >= 0) ? (1UL << cpu) : 0UL;
}

// 执行 task_start 之后，调用此函数发送 IPI，通知目标 cpu 切换任务
// 不操作当前 cpu，调用者应保证禁用抢占
void notify_resched(uint64_t cpumask) {
    cpumask &= ~(1UL << cpu_index());
    while (cpumask) {
        int cpu = __builtin_ctzll(cpumask);
        ASSERT(cpu_index() != cpu);
        cpumask &= cpumask - 1;
        arch_send_ipi(cpu, VEC_IPI_RESCHED);
    }
}


//------------------------------------------------------------------------------

static NORETURN void task_entry(void (*real)()) {
    task_t *self = THISCPU_GET(g_tid_prev);
    real();
    logk("task %s exit\n", self->name);
    task_exit();
    logk("task %s still running!\n", self->name);
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

static NORETURN void proc_idle() {
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}
