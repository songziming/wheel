#include <wheel.h>

// P (proberen), a.k.a. take / down
// V (verhogen), a.k.a. give / up

typedef struct sem_waiter {
    dlnode_t dl;
    task_t * tid;
    int      acquired;
} sem_waiter_t;

void sema_init(sema_t * sem, int limit, int value) {
    assert(value <= limit);
    sem->lock   = SPIN_INIT;
    sem->pend_q = DLLIST_INIT;
    sem->limit  = limit;
    sem->value  = value;
}

// Linux doesn't have this function, is it useless?
#if 0
// resume all pending tasks waiting for this sema
// waiters still remain inside `sem->pend_q`
// unpended tasks still need this sema to check unpend reason
// parent structure could use ref-counting to manage object life cycle
void sema_freeall(sema_t * sem) {
    u32 key = irq_spin_take(&sem->lock);

    for (dlnode_t * dl = sem->pend_q.head; NULL != dl; dl = dl->next) {
        sem_waiter_t * waiter = PARENT(dl, sem_waiter_t, dl);
        task_t       * tid    = waiter->tid;

        raw_spin_take(&tid->lock);
        waiter->acquired = NO;
        sched_cont(tid, TS_PEND);
        int cpu = tid->last_cpu;
        raw_spin_give(&tid->lock);

        if (cpu_index() != cpu) {
            smp_reschedule(cpu);
        }
    }

    irq_spin_give(&sem->lock, key);
    task_switch();
}
#endif

// this function is executed under ISR
static void sema_timeout(task_t * tid, int * expired) {
    // variable `expired` is on the stack of `tid`,
    // which is also protected by `tid->lock`
    u32 key = irq_spin_take(&tid->lock);
    sched_cont(tid, TS_PEND);
    * expired = YES;
    irq_spin_give(&tid->lock, key);
}

// return OK if successfully taken the sema
// return ERROR if failed (might block)
// this function cannot be called inside ISR
int sema_take(sema_t * sem, int timeout) {
    assert(NULL != sem);
    assert(0 == thiscpu_var(int_depth));

    preempt_lock();
    raw_spin_take(&sem->lock);

    // check if we can take this sema
    if (sem->value > 0) {
        --sem->value;
        raw_spin_give(&sem->lock);
        preempt_unlock();
        return OK;
    }

    // resource not available, pend current task
    task_t * tid = thiscpu_var(tid_prev);
    raw_spin_take(&tid->lock);

    // prepare waiter structure
    sem_waiter_t waiter;
    waiter.dl       = DLNODE_INIT;
    waiter.tid      = tid;
    waiter.acquired = NO;
    dl_push_tail(&sem->pend_q, &waiter.dl);

    // prepare timeout watchdog
    int expired = NO;
    wdog_t wd;
    wdog_init(&wd);

    // start wdog while holding `tid->lock`
    // so that timeout will not happen before pending
    if (timeout != SEM_WAIT_FOREVER) {
        wdog_start(&wd, timeout, sema_timeout, tid, &expired, 0,0);
    }

    // pend this task
    sched_stop(tid, TS_PEND);
    raw_spin_give(&tid->lock);
    raw_spin_give(&sem->lock);
    preempt_unlock();

    // pend here
    task_switch();

    // stop timer as soon as we unpend
    wdog_cancel(&wd);

    // lock sema again, so other tasks cannot give or freeall
    preempt_lock();
    raw_spin_take(&sem->lock);

    // check success condition first
    // since timeout might overlap with acquire and delete
    if (YES == waiter.acquired) {
        raw_spin_give(&sem->lock);
        preempt_unlock();
        return OK;
    }

    // sema not acquired, waiter still in pend_q
    dl_remove(&sem->pend_q, &waiter.dl);

    // TODO: if we have no `freeall`, no signal,
    //       then no need to check `expired`
    //       not acquired just means timeout
#if 0
    // check if we have expired
    if (expired) {
        raw_spin_give(&sem->lock);
        preempt_unlock();
        return ERROR;
    }
#endif

    // if sema got destroyed
    raw_spin_give(&sem->lock);
    preempt_unlock();
    return ERROR;
}

// this function can be called during ISR
int sema_trytake(sema_t * sem) {
    u32 key = irq_spin_take(&sem->lock);
    if (sem->value > 0) {
        --sem->value;
        irq_spin_give(&sem->lock, key);
        return OK;
    }
    irq_spin_give(&sem->lock, key);
    return ERROR;
}

// this function can be called inside ISR
void sema_give(sema_t * sem) {
    u32 key = irq_spin_take(&sem->lock);

    dlnode_t * dl = dl_pop_head(&sem->pend_q);
    if (NULL == dl) {
        if (sem->value < sem->limit) {
            ++sem->value;
        }
        irq_spin_give(&sem->lock, key);
        return;
    }

    sem_waiter_t * waiter = PARENT(dl, sem_waiter_t, dl);
    task_t * tid = waiter->tid;

    // wake up this task
    raw_spin_take(&tid->lock);
    waiter->acquired = YES;
    sched_cont(tid, TS_PEND);
    int cpu = tid->last_cpu;
    raw_spin_give(&tid->lock);
    irq_spin_give(&sem->lock, key);

    if (cpu_index() == cpu) {
        task_switch();
    } else {
        smp_reschedule(cpu);
    }
}
