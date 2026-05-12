#include "task.h"
#include "work.h"
#include <arch_api.h>
#include <spin.h>
#include <dllist.h>
#include <kstring.h>
#include <debug.h>

// #define LOG_SCHED 1

#define PRIORITY_NUM 32

typedef struct rdyq {
    spin_t      lock;
    dlnode_t   *heads[PRIORITY_NUM];
    uint32_t    priorities; // mask
    int         count;
} rdyq_t;

static PERCPU_BSS rdyq_t g_rdyq;
static PERCPU_BSS task_t g_idle_tcb;
static INIT_BSS task_t g_dummy_tcb;

// 负载均衡
static _Atomic uint64_t g_idle_mask = 0UL;
static _Atomic uint32_t g_next_cpu = 0U;

PERCPU_DATA int g_preempt_depth = 0;
PERCPU_BSS task_t *g_tid_prev; // updated in int_return
PERCPU_BSS task_t *g_tid_next; // guarded by rdyq->lock

//------------------------------------------------------------------------------
// 就绪队列函数，全部无锁
//------------------------------------------------------------------------------

INIT_TEXT void rdyq_init(rdyq_t *q) {
    q->lock = SPIN_INIT;
    kmemset(q->heads, 0, sizeof(q->heads));
    q->priorities = 0U;
    q->count = 0;
}

void rdyq_insert(rdyq_t *q, dlnode_t *dl, int prio) {
    if (NULL == q->heads[prio]) {
        dl_init_circular(dl);
        q->heads[prio] = dl;
        q->priorities |= 1U << prio;
    } else {
        dl_insert_before(dl, q->heads[prio]);
    }
}

void rdyq_remove(rdyq_t *q, dlnode_t *dl, int prio) {
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

dlnode_t *rdyq_head(rdyq_t *q) {
    int prio = __builtin_ctz(q->priorities);
    return q->heads[prio];
}

//------------------------------------------------------------------------------
// 禁用抢占但允许中断
//------------------------------------------------------------------------------

void preempt_lock() {
    THISCPU_ADD(g_preempt_depth, 1);
}

void preempt_unlock() {
    THISCPU_ADD(g_preempt_depth, -1);
}

//------------------------------------------------------------------------------
// 任务状态切换，改变 tid_next 实现抢占
//------------------------------------------------------------------------------

static void _cont_this(task_t *tid) {
    rdyq_t *q = THISCPU(&g_rdyq);
    int key = irq_spin_take(&q->lock);

    rdyq_insert(q, &tid->dl, tid->priority);
    if (tid->priority < THISCPU_GET(g_tid_next)->priority) {
#if defined(LOG_SCHED) && LOG_SCHED
        logk("<preempt %s>", tid->name);
#endif
        THISCPU_SET(g_tid_next, tid);
    }

    irq_spin_give(&q->lock, key);
}

static void _cont_cpu(task_t *tid, int cpu) {
    rdyq_t *q = PERCPU(cpu, &g_rdyq);
    int key = irq_spin_take(&q->lock);

    rdyq_insert(q, &tid->dl, tid->priority);
    if (tid->priority < (*PERCPU(cpu, &g_tid_next))->priority) {
        *PERCPU(cpu, &g_tid_next) = tid;
    }

    irq_spin_give(&q->lock, key);
}

// 恢复运行这个 task，返回需要切换任务的 CPU-mask
// 启动任务、semaphore/fence 恢复任务时调用此函数
uint64_t sched_cont(task_t *tid, uint32_t bits) {
    ASSERT(TS_READY != tid->state);
    // ASSERT(tid->affinity < 0 || tid->affinity == cpu_index());

    tid->state &= ~bits;
    if (TS_READY != tid->state) {
        logk("task %s cannot preempt %x\n", tid->name, tid->state);
        return 0; // still no ready
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

    atomic_fetch_and(&g_idle_mask, ~(1UL << cpu));

    // 返回 cpu-mask，这样批量恢复任务时（如 semaphore、fence）
    // 就可以最后统一发送 IPI
    return 1ULL << cpu;
}


//------------------------------------------------------------------------------
// 任务停止运行，只能停止当前任务
//------------------------------------------------------------------------------

void task_stop(uint32_t bits) {
    task_t *self = THISCPU_GET(g_tid_prev);
    rdyq_t *q = THISCPU(&g_rdyq);

    self->state |= bits;

    int key = irq_spin_take(&q->lock);
    rdyq_remove(q, &self->dl, self->priority);
    task_t *next = containerof(rdyq_head(q), task_t, dl);
    THISCPU_SET(g_tid_next, next);

#if defined(LOG_SCHED) && LOG_SCHED
    logk("curr %s stop, next is %s\n", self->name, next->name);
#endif

    if (31 == next->priority) {
#if defined(LOG_SCHED) && LOG_SCHED
        logk("cpu-%d idle again\n", cpu_index());
#endif
        atomic_fetch_or(&g_idle_mask, 1UL << cpu_index());
    }

    // TODO 在这里将 self 放在 pend-queue 里面

    irq_spin_give(&q->lock, key);
}

//------------------------------------------------------------------------------
// 退出自身任务
//------------------------------------------------------------------------------

typedef struct freework {
    work_t wk;
    task_t *tid;
} freework_t;

// work 函数还要访问 next
static void task_free(work_t *wk) {
    // work 也在函数栈上，删除映射后，不能再访问 work
    // 所以提前读出 task，后面直接用 tid
    freework_t *work = containerof(wk, freework_t, wk);
    task_t *tid = work->tid;
#if defined(LOG_SCHED) && LOG_SCHED
    logk("free task %s\n", tid->name);
#endif
    // logk("stack at va:%zx pa:%zx\n", tid->stack.vaddr, tid->stack.paddr);
    // logk("work at %p, tcb at %p\n", work, tid);

    vmspace_remove(&g_kernel_vm, &tid->stack);
    tid->state = TS_DELETED;
}

// 全程必须关闭中断，防止被抢占，否则 work 没来得及注册，任务无法回收
// TODO 大部分逻辑和 task_stop 一样，应该统一
void task_exit() {
    task_t *self = THISCPU_GET(g_tid_prev);

    // 发送 IPI，让其他 cpu 清楚当前 task-stack
    // 这样只剩当前 cpu 还保留 mapping，留到 work 里面删除
    tlb_shootdown(self->stack.vaddr, self->stack.vend);

    rdyq_t *q = THISCPU(&g_rdyq);
    int key = irq_spin_take(&q->lock);

    self->state = TS_STOPPED;
    rdyq_remove(q, &self->dl, self->priority);

    task_t *next = containerof(rdyq_head(q), task_t, dl);
    THISCPU_SET(g_tid_next, next);
    if (31 == next->priority) {
        atomic_fetch_or(&g_idle_mask, 1UL << cpu_index());
    }

    freework_t freework;
    freework.tid = self;
    work_defer(&freework.wk, task_free, "freetask");

    irq_spin_give(&q->lock, key);
    arch_task_switch();
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

INIT_TEXT void sched_init() {
    rdyq_t *q = THISCPU(&g_rdyq);
    rdyq_init(q);

    task_t *idle = THISCPU(&g_idle_tcb);
    task_create(idle, "idle", 31, proc_idle);
    idle->affinity = cpu_index();
    idle->state = TS_READY;
    rdyq_insert(q, &idle->dl, 31);
    // task_start(idle);
    g_idle_mask |= 1UL << cpu_index();

    g_dummy_tcb.priority = 33; // 确保能抢占
    THISCPU_SET(g_tid_prev, &g_dummy_tcb);
    THISCPU_SET(g_tid_next, idle);
}

// called in timer
// 只负责轮转，不抢占（抢占通过 arch_task_switch 触发）
void sched_process() {
    ASSERT(cpu_int_depth() > 0);
    rdyq_t *q = THISCPU(&g_rdyq);
    raw_spin_take(&q->lock);

    task_t *prev = THISCPU_GET(g_tid_next);
    task_t *next = containerof(prev->dl.next, task_t, dl);
    if (prev != next) {
#if defined(LOG_SCHED) && LOG_SCHED
        logk("[%s->%s]", prev->name, next->name);
#endif
        THISCPU_SET(g_tid_next, next);
        // logk("(%s:%p->%s:%p)", prev->name, prev, next->name, next);

        // size_t prev_stk = (size_t)prev->stack_top >> PAGE_SHIFT;
        // size_t next_stk = (size_t)next->stack_top >> PAGE_SHIFT;
        // if (prev_stk == next_stk) {
        //     logk("same stack!\n");
        //     ASMV("xchgw %bx, %bx");
        //     while (1) { cpu_halt(); }
        // }
    }

    raw_spin_give(&q->lock);
}

void task_create(task_t *tid, const char *name, int prio, void *func) {
    tid->state    = TS_STOPPED;
    tid->name     = name;
    tid->priority = prio;
    tid->affinity = -1;

    vmspace_alloc_stack(&g_kernel_vm, &tid->stack, 0);
    tid->stack.desc = name;
    // logk("%s stack at va:%zx -> pa:%zx\n", name, tid->stack.vaddr, tid->stack.paddr);
    arch_task_init(tid, (size_t)task_entry, tid->stack.vend, (size_t)func,0,0,0);
}

uint64_t task_start(task_t *tid) {
    return sched_cont(tid, TS_STOPPED);
}


// 执行 task_start 之后，调用此函数发送 IPI，通知目标 cpu 切换任务
// 不操作当前 cpu，调用者应保证禁用抢占
void notify_resched(uint64_t cpumask) {
    // int has_local = 0;
    // uint64_t this_mask = 1UL << cpu_index();
    // if (cpumask & this_mask) {
    //     has_local = 1;
    //     cpumask &= ~this_mask;
    // }
    cpumask &= ~(1UL << cpu_index());
    while (cpumask) {
        int cpu = __builtin_ctzll(cpumask);
        ASSERT(cpu_index() != cpu);
        cpumask &= cpumask - 1;
        arch_send_ipi(cpu, VEC_IPI_RESCHED);
    }
}