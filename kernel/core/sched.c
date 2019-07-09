#include <wheel.h>

typedef struct ready_q {
    spin_t   lock;
    int      load;                  // number of tasks
    u32      priorities;            // bit mask
    dllist_t tasks[PRIORITY_COUNT]; // protected by ready_q.lock
} ready_q_t;

static __PERCPU ready_q_t ready_q;

__PERCPU u32      no_preempt;
__PERCPU task_t * tid_prev;
__PERCPU task_t * tid_next;        // protected by ready_q.lock

//------------------------------------------------------------------------------
// helper function

// find the cpu with lowest current priority, if multiple cpu have
// the lowest priority, then find the one with least load.
// prefer original cpu
static int find_lowest_cpu(task_t * tid) {
    int lowest_cpu;
    int lowest_pri;
    int lowest_load;

    // find the cpu with lowest priority
    if (-1 != tid->last_cpu) {
        lowest_cpu = tid->last_cpu;
        lowest_pri = CTZ32(percpu_ptr(lowest_cpu, ready_q)->priorities);
    } else {
        lowest_cpu = -1;
        lowest_pri = PRIORITY_IDLE+1;
    }
    for (int i = 0; i < cpu_activated; ++i) {
        ready_q_t * rdy = percpu_ptr(i, ready_q);
        int         pri = CTZ32(rdy->priorities);
        if (pri < lowest_pri) {
            lowest_cpu  = i;
            lowest_pri  = pri;
        }
    }

    // check whether we can preempt the lowest priority cpu
    if (tid->priority < lowest_pri) {
        return lowest_cpu;
    }

    // if can't preempt, choose the lowest loaded cpu instead
    if (-1 != tid->last_cpu) {
        lowest_cpu  = tid->last_cpu;
        lowest_load = percpu_ptr(lowest_cpu, ready_q)->load;
    } else {
        lowest_cpu  = -1;
        lowest_load = 0x7fffffff;
    }
    for (int i = 0; i < cpu_activated; ++i) {
        ready_q_t * rdy = percpu_ptr(i, ready_q);
        if (rdy->load < lowest_load) {
            lowest_cpu  = i;
            lowest_load = rdy->load;
        }
    }

    return lowest_cpu;
}

// ready queue is already locked
static task_t * find_highest_task(ready_q_t * rdy) {
    dbg_assert(0 != rdy->priorities);
    int pri = CTZ32(rdy->priorities);
    return PARENT(rdy->tasks[pri].head, task_t, dl_sched);
}

//------------------------------------------------------------------------------
// low level scheduling, task state switching
// caller need to lock interrupt, or risk being switched-out
// caller also need to lock target tid, or might be deleted by others
// after unlocking interrupt, call `task_switch` or `smp_reschedule` manually

// add bits to `tid->state`, possibly stopping it, return old state.
// this function only updates `tid`, `ready_q`, and `tid_next`.
u32 sched_stop(task_t * tid, u32 bits) {
    // change state, return if already stopped
    u32 state   = tid->state;
    tid->state |= bits;
    if (TS_READY != state) {
        return state;
    }

    int pri = tid->priority;
    int cpu = tid->last_cpu;
    ready_q_t * rdy = percpu_ptr(cpu, ready_q);
    raw_spin_take(&rdy->lock);

    // remove task from ready queue
    dl_remove(&rdy->tasks[pri], &tid->dl_sched);
    rdy->load -= 1;
    if (dl_is_empty(&rdy->tasks[pri])) {
        rdy->priorities &= ~(1U << pri);
    }

    // if this task is running, pick a new one
    if (tid == percpu_var(cpu, tid_next)) {
        percpu_var(cpu, tid_next) = find_highest_task(rdy);
    }

    // after this point, `tid_next` might be changed again
    raw_spin_give(&rdy->lock);
    return state;
}

// remove bits from `tid->state`, possibly resuming the task.
// return previous task state.
// this function only updates `tid`, `ready_q`, and `tid_next` only,
// caller should call `task_switch` or `smp_reschedule` manually.
u32 sched_cont(task_t * tid, u32 bits) {
    // change state, return if already running
    u32 state   = tid->state;
    tid->state &= ~bits;
    if ((TS_READY == state) || (TS_READY != tid->state)) {
        return state;
    }

    int pri = tid->priority;
    int cpu = find_lowest_cpu(tid);
    ready_q_t * rdy = percpu_ptr(cpu, ready_q);
    raw_spin_take(&rdy->lock);

    // put task back into the ready queue
    dl_push_tail(&rdy->tasks[pri], &tid->dl_sched);
    tid->last_cpu    = cpu;
    rdy->load       += 1;
    rdy->priorities |= 1U << pri;

    // check whether we can preempt
    task_t * old = percpu_var(cpu, tid_next);
    if (pri < old->priority) {
        percpu_var(cpu, tid_next) = tid;
    }

    // after this point, `tid_next` might be changed again
    raw_spin_give(&rdy->lock);
    return state;
}

//------------------------------------------------------------------------------
// scheduler operations

// disable task preemption
void preempt_lock() {
    thiscpu32_inc(&no_preempt);
}

// re-enable task preemption
// caller should invoke `task_switch`
void preempt_unlock() {
    thiscpu32_dec(&no_preempt);
}

// this function might be called during tick_advance
// task state not changed, no need to lock current tid
void sched_yield() {
    ready_q_t * rdy = thiscpu_ptr(ready_q);
    u32         key = irq_spin_take(&rdy->lock);

    int      pri = CTZ32(rdy->priorities);
    task_t * tid = PARENT(rdy->tasks[pri].head, task_t, dl_sched);

    // round robin only if current task is the head task
    if (thiscpu_var(tid_prev) == tid) {
        dl_remove   (&rdy->tasks[pri], &tid->dl_sched);
        dl_push_tail(&rdy->tasks[pri], &tid->dl_sched);
        tid = PARENT(rdy->tasks[pri].head, task_t, dl_sched);
    }
    thiscpu_var(tid_next) = tid;

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
    // this function is called before starting all cpu
    // so we should use `cpu_installed` instead of `cpu_activated`
    for (int i = 0; i < cpu_installed; ++i) {
        percpu_var(i, tid_prev)   = NULL;
        percpu_var(i, tid_prev)   = NULL;
        percpu_var(i, no_preempt) = 0;

        ready_q_t * rdy = percpu_ptr(i, ready_q);
        rdy->lock       = SPIN_INIT;
        rdy->load       = 1;        // idle task
        rdy->priorities = 1U << 31; // idle task

        for (int p = 0; p < PRIORITY_COUNT; ++p) {
            rdy->tasks[p] = DLLIST_INIT;
        }

        char name[] = "idle-x";
        name[5] = '0' + i;
        task_t * idle = task_create(name, PRIORITY_IDLE, idle_proc, 0,0,0,0);
        idle->state    = TS_READY;
        idle->affinity = 1UL << i;
        idle->last_cpu = i;
        dl_push_tail(&rdy->tasks[PRIORITY_IDLE], &idle->dl_sched);
    }
}
