#include <wheel.h>

#ifdef DEBUG
static int lib_initialized = NO;
#endif

//------------------------------------------------------------------------------
// ready queue

typedef struct ready_q {
    spin_t      spin;
    int         count;
    u32         priorities;
    dllist_t    tasks[PRIORITY_COUNT];
} ready_q_t;

static void ready_q_push(ready_q_t * rdy, task_t * tid) {
    int pri = tid->priority;
    dl_push_tail(&rdy->tasks[pri], &tid->dl_sched);
    rdy->priorities |= 1U << pri;
    ++rdy->count;
}

static task_t * ready_q_pop(ready_q_t * rdy) {
    assert(0 != rdy->count);
    assert(0 != rdy->priorities);
    int        pri = CTZ32(rdy->priorities);
    dlnode_t * dl  = dl_pop_head(&rdy->tasks[pri]);
    if (dl_is_empty(&rdy->tasks[pri])) {
        rdy->priorities &= ~(1U << pri);
    }
    --rdy->count;
    return PARENT(dl, task_t, dl_sched);
}

static task_t * ready_q_head(ready_q_t * rdy) {
    assert(0 != rdy->count);
    assert(0 != rdy->priorities);
    int pri = CTZ32(rdy->priorities);
    return PARENT(rdy->tasks[pri].head, task_t, dl_sched);
}

static void ready_q_remove(ready_q_t * rdy, task_t * tid) {
    int pri = tid->priority;
    dl_remove(&rdy->tasks[pri], &tid->dl_sched);
    if (dl_is_empty(&rdy->tasks[pri])) {
        rdy->priorities &= ~(1U << pri);
    }
    --rdy->count;
}

//------------------------------------------------------------------------------
// scheduler

static __PERCPU ready_q_t ready_q;

__PERCPU int      no_preempt = 0;
__PERCPU task_t * tid_prev   = NULL;
__PERCPU task_t * tid_next   = NULL;    // also protected by sched_lock

// find the cpu with lowest current priority, if multiple cpu have
// the lowest priority, then find the one with least load.
static int find_lowest_cpu(task_t * tid) {
    int lowest_cpu  = -1;
    int lowest_pri  = PRIORITY_IDLE+1;
    int lowest_load = 0x7fffffff;

    // prefer original cpu
    if (-1 != tid->last_cpu) {
        ready_q_t * rdy = percpu_ptr(tid->last_cpu, ready_q);
        lowest_cpu  = tid->last_cpu;
        lowest_pri  = CTZ32(rdy->priorities);
        lowest_load = rdy->count;
    }

    // find the cpu with lowest priority
    for (int i = 0; i < cpu_count(); ++i) {
        ready_q_t * rdy = percpu_ptr(i, ready_q);
        int         pri = CTZ32(rdy->priorities);
        if (pri > lowest_pri) {
            continue;
        }
        if ((pri == lowest_pri) && (rdy->count >= lowest_load)) {
            continue;
        }
        lowest_cpu  = i;
        lowest_pri  = pri;
        lowest_load = rdy->count;
    }

    return lowest_cpu;
}

// add bits to `tid->state`, possibly stopping it, return old state.
// this function only updates `tid`, `ready_q`, and `tid_next`.
u32 sched_stop(task_t * tid, u32 bits) {
#ifdef DEBUG
    if (YES != lib_initialized) {
        panic("sched lib not initialized.\n");
    }
#endif

    // change state, return if already stopped
    u32 state = tid->state;
    tid->state |= bits;
    if (TS_READY != state) {
        return state;
    }

    // lock current ready queue
    int cpu = tid->last_cpu;
    ready_q_t * rdy = percpu_ptr(cpu, ready_q);
    raw_spin_take(&rdy->spin);

    // remove task and update tid_next
    ready_q_remove(rdy, tid);
    percpu_var(cpu, tid_next) = ready_q_head(rdy);

    // after this point, `tid_next` might be changed again
    raw_spin_give(&rdy->spin);
    return state;
}

// remove bits from `tid->state`, possibly resuming it, return old state.
// this function only updates `tid`, `ready_q`, and `tid_next`.
u32 sched_cont(task_t * tid, u32 bits) {
#ifdef DEBUG
    if (YES != lib_initialized) {
        panic("sched lib not initialized.\n");
    }
#endif

    // change state, return if already running
    u32 state = tid->state;
    tid->state &= ~bits;
    if ((TS_READY == state) || (TS_READY != tid->state)) {
        return state;
    }

    // pick and lock a target ready queue
    int cpu = find_lowest_cpu(tid);
    ready_q_t * rdy = percpu_ptr(cpu, ready_q);
    raw_spin_take(&rdy->spin);

    // push task and update tid_next
    ready_q_push(rdy, tid);
    tid->last_cpu = cpu;
    percpu_var(cpu, tid_next) = ready_q_head(rdy);

    // after this point, `tid_next` might be changed again
    raw_spin_give(&rdy->spin);
    return state;
}

//------------------------------------------------------------------------------
// scheduler operations

// disable task preemption
void preempt_lock() {
    atomic32_inc(thiscpu_ptr(no_preempt));
}

// re-enable task preemption, caller needs to call `task_switch` after this
void preempt_unlock() {
    atomic32_dec(thiscpu_ptr(no_preempt));
}

// this function might be called during tick_advance
// task state not changed, no need to lock current tid
void sched_yield() {
#ifdef DEBUG
    if (YES != lib_initialized) {
        panic("sched lib not initialized.\n");
    }
#endif

    ready_q_t * rdy = thiscpu_ptr(ready_q);
    u32         key = irq_spin_take(&rdy->spin);
    task_t    * tid = ready_q_head(rdy);

    // round robin only if current task is the head task
    if (thiscpu_var(tid_prev) == tid) {
        ready_q_pop(rdy);
        ready_q_push(rdy, tid);
    }
    thiscpu_var(tid_next) = ready_q_head(rdy);

    irq_spin_give(&rdy->spin, key);
    task_switch();
}

// this function is called during clock interrupt
// so current task is not executing
void sched_tick() {
    task_t * tid = thiscpu_var(tid_prev);
    if (tid->priority == PRIORITY_IDLE) {
        return;
    }
    if (--tid->remaining <= 0) {
        tid->remaining = tid->timeslice;
        sched_yield();
    }
}

//------------------------------------------------------------------------------
// initialize scheduler

static void idle_proc() {
    // lock current task and never give away
    raw_spin_take(&thiscpu_var(tid_prev)->spin);

    // loop forever
    while (1) {
        cpu_sleep();
    }
}

__INIT void sched_lib_init() {
#ifdef DEBUG
    if (YES == lib_initialized) {
        panic("sched lib already initialized.\n");
    }
    lib_initialized = YES;
#endif

    for (int i = 0; i < cpu_count(); ++i) {
        ready_q_t * rdy = percpu_ptr(i, ready_q);
        rdy->spin       = SPIN_INIT;
        rdy->count      = 0;
        rdy->priorities = 1U << PRIORITY_IDLE;
        for (int i = 0; i < PRIORITY_COUNT; ++i) {
            rdy->tasks[i] = DLLIST_INIT;
        }

        task_t * idle  = task_create(PRIORITY_IDLE, idle_proc, 0,0,0,0);
        idle->state    = TS_READY;
        idle->affinity = 1UL << i;
        idle->last_cpu = i;
        ready_q_push(rdy, idle);

        percpu_var(i, tid_prev)   = NULL;
        percpu_var(i, tid_next)   = idle;
        percpu_var(i, no_preempt) = 0;
    }
}
