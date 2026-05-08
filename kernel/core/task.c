#include "task.h"
#include "work.h"
#include <arch_api.h>
#include <spin.h>
#include <dllist.h>
#include <kstring.h>
#include <debug.h>

// #include <apic/apic.h>



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
// 任务状态切换，改变 tid_next 实现抢占
//------------------------------------------------------------------------------

// 在当前 CPU 恢复运行这个 task
// 但不要立即触发 task-switch
void sched_cont(task_t *tid, uint32_t bits) {
    ASSERT(TS_READY != tid->state);
    ASSERT(tid->affinity < 0 || tid->affinity == cpu_index());

    tid->state &= ~bits;
    if (TS_READY != tid->state) {
        logk("task %s cannot preempt %x\n", tid->name, tid->state);
        return; // still no ready
    }

    rdyq_t *q = THISCPU(&g_rdyq);
    int key = irq_spin_take(&q->lock);

    rdyq_insert(q, &tid->dl, tid->priority);
    if (tid->priority < THISCPU_GET(g_tid_next)->priority) {
        logk("<preempt-%d=%s>", cpu_index(), tid->name);
        THISCPU_SET(g_tid_next, tid);
    }

    irq_spin_give(&q->lock, key);
}


//------------------------------------------------------------------------------
// 退出自身任务
//------------------------------------------------------------------------------

typedef struct freework {
    work_t wk;
    task_t *tid;
} freework_t;

static void task_free(work_t *wk) {
    freework_t *work = containerof(wk, freework_t, wk);
    logk("free task %s\n", work->tid->name);
    vmspace_remove(&g_kernel_vm, &work->tid->stack);
    work->tid->state = TS_DELETED;
}

// 全程必须关闭中断，防止被抢占，否则 work 没来得及注册，任务无法回收
void task_exit() {
    task_t *self = THISCPU_GET(g_tid_prev);

    rdyq_t *q = THISCPU(&g_rdyq);
    int key = irq_spin_take(&q->lock);

    self->state = TS_STOPPED;
    rdyq_remove(q, &self->dl, self->priority);

    task_t *next = containerof(rdyq_head(q), task_t, dl);
    THISCPU_SET(g_tid_next, next);

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
    rdyq_insert(q, &idle->dl, 31);

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

    task_t *prev = THISCPU_GET(g_tid_prev);
    task_t *next = containerof(prev->dl.next, task_t, dl);
    // logk("(%s->%s)", prev->name, next->name);
    THISCPU_SET(g_tid_next, next);

    raw_spin_give(&q->lock);
}

void task_create(task_t *tid, const char *name, int prio, void *func) {
    tid->state    = TS_STOPPED;
    tid->name     = name;
    tid->priority = prio;
    tid->affinity = -1;

    vmspace_alloc_stack(&g_kernel_vm, &tid->stack, 0);
    tid->stack.desc = name;
    logk("%s stack at %zx\n", name, tid->stack.vaddr);
    arch_task_init(tid, (size_t)task_entry, tid->stack.vend, (size_t)func,0,0,0);
}

void task_start(task_t *tid) {
    sched_cont(tid, TS_STOPPED);
}
