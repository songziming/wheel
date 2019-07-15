#include <wheel.h>

//------------------------------------------------------------------------------
// turnstile

void turnstile_init(turnstile_t * ts) {
    ts->count = 0;
    ts->priorities = 0;
    for (int i = 0; i < PRIORITY_COUNT; ++i) {
        ts->tasks[i] = DLLIST_INIT;
    }
}

void turnstile_push(turnstile_t * ts, task_t * tid) {
    int pri = tid->priority;
    dl_push_tail(&ts->tasks[pri], &tid->dl_sched);
    ts->priorities |= 1U << pri;
    ++ts->count;
}

void turnstile_remove(turnstile_t * ts, task_t * tid) {
    int pri = tid->priority;
    dl_remove(&ts->tasks[pri], &tid->dl_sched);
    if (dl_is_empty(&ts->tasks[pri])) {
        ts->priorities &= ~(1U << pri);
    }
    --ts->count;
}

task_t * turnstile_peek(turnstile_t * ts) {
    int pri = CTZ32(ts->priorities);
    return PARENT(ts->tasks[pri].head, task_t, dl_sched);
}

task_t * turnstile_pop(turnstile_t * ts) {
    int        pri = CTZ32(ts->priorities);
    dlnode_t * dl  = dl_pop_head(&ts->tasks[pri]);
    if (dl_is_empty(&ts->tasks[pri])) {
        ts->priorities &= ~(1U << pri);
    }
    --ts->count;
    return PARENT(dl, task_t, dl_sched);
}

//------------------------------------------------------------------------------
// ready queue

typedef struct ready_q {
    spin_t      lock;
    turnstile_t ts;
} ready_q_t;

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
        lowest_pri  = CTZ32(rdy->ts.priorities);
        lowest_load = rdy->ts.count;
    }

    // find the cpu with lowest priority
    for (int i = 0; i < cpu_count(); ++i) {
        ready_q_t * rdy = percpu_ptr(i, ready_q);
        int         pri = CTZ32(rdy->ts.priorities);
        if (pri > lowest_pri) {
            continue;
        }
        if ((pri == lowest_pri) && (rdy->ts.count >= lowest_load)) {
            continue;
        }
        lowest_cpu  = i;
        lowest_pri  = pri;
        lowest_load = rdy->ts.count;
    }

    return lowest_cpu;
}

// add bits to `tid->state`, possibly stopping it, return old state.
// this function only updates `tid`, `ready_q`, and `tid_next`.
u32 sched_stop(task_t * tid, u32 bits) {
    // change state, return if already stopped
    u32 state   = tid->state;
    tid->state |= bits;
    if (TS_READY != state) {
        return state;
    }

    // lock current ready queue
    int cpu = tid->last_cpu;
    ready_q_t * rdy = percpu_ptr(cpu, ready_q);
    raw_spin_take(&rdy->lock);

    // remove task and update tid_next
    turnstile_remove(&rdy->ts, tid);
    percpu_var(cpu, tid_next) = turnstile_peek(&rdy->ts);

    // after this point, `tid_next` might be changed again
    raw_spin_give(&rdy->lock);
    return state;
}

// remove bits from `tid->state`, possibly resuming it, return old state.
// this function only updates `tid`, `ready_q`, and `tid_next`.
u32 sched_cont(task_t * tid, u32 bits) {
    // change state, return if already running
    u32 state   = tid->state;
    tid->state &= ~bits;
    if ((TS_READY == state) || (TS_READY != tid->state)) {
        return state;
    }

    // pick and lock a target ready queue
    int cpu = find_lowest_cpu(tid);
    ready_q_t * rdy = percpu_ptr(cpu, ready_q);
    raw_spin_take(&rdy->lock);

    // push task and update tid_next
    turnstile_push(&rdy->ts, tid);
    tid->last_cpu = cpu;
    percpu_var(cpu, tid_next) = turnstile_peek(&rdy->ts);

    // after this point, `tid_next` might be changed again
    raw_spin_give(&rdy->lock);
    return state;
}

//------------------------------------------------------------------------------
// scheduler operations

// disable task preemption
void preempt_lock() {
    atomic32_inc((u32 *) thiscpu_ptr(no_preempt));
}

// re-enable task preemption, caller needs to call `task_switch` after this
void preempt_unlock() {
    atomic32_dec((u32 *) thiscpu_ptr(no_preempt));
}

// this function might be called during tick_advance
// task state not changed, no need to lock current tid
void sched_yield() {
    task_t    * tid = thiscpu_var(tid_prev);
    ready_q_t * rdy = thiscpu_ptr(ready_q);
    u32         key = irq_spin_take(&rdy->lock);

    // round robin only if current task is the head task
    if (turnstile_peek(&rdy->ts) == tid) {
        turnstile_remove(&rdy->ts, tid);
        turnstile_push  (&rdy->ts, tid);
        thiscpu_var(tid_next) = turnstile_peek(&rdy->ts);
    }

    irq_spin_give(&rdy->lock, key);
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
    raw_spin_take(&thiscpu_var(tid_prev)->lock);

    // loop forever
    while (1) {
        cpu_sleep();
    }
}

__INIT void sched_lib_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        task_t * idle  = task_create(PRIORITY_IDLE, idle_proc, 0,0,0,0);
        idle->state    = TS_READY;
        idle->affinity = 1UL << i;
        idle->last_cpu = i;

        ready_q_t * rdy = percpu_ptr(i, ready_q);
        turnstile_init(&rdy->ts);
        turnstile_push(&rdy->ts, idle);
        rdy->lock = SPIN_INIT;

        percpu_var(i, tid_next) = idle;
    }
}
