#include <wheel.h>

// P (proberen), a.k.a. take / down
// V (verhogen), a.k.a. give / up

typedef struct sem_waiter {
    dlnode_t dl;
    task_t * tid;
    int      acquired;
} sem_waiter_t;

void sema_init(sema_t * sema, int limit, int value) {
    assert(value <= limit);
    sema->spin   = SPIN_INIT;
    sema->pend_q = DLLIST_INIT;
    sema->limit  = limit;
    sema->value  = value;
}

// Linux doesn't have this function, is it useless?
#if 0
// resume all pending tasks waiting for this sema
// waiters still remain inside `sema->pend_q`
// unpended tasks still need this sema to check unpend reason
// parent structure could use ref-counting to manage object life cycle
void sema_freeall(sema_t * sema) {
    u32 key = irq_spin_take(&sema->spin);

    for (dlnode_t * dl = sema->pend_q.head; NULL != dl; dl = dl->next) {
        sem_waiter_t * waiter = PARENT(dl, sem_waiter_t, dl);
        task_t       * tid    = waiter->tid;

        raw_spin_take(&tid->spin);
        waiter->acquired = NO;
        sched_cont(tid, TS_PEND);
        int cpu = tid->last_cpu;
        raw_spin_give(&tid->spin);

        if (cpu_index() != cpu) {
            smp_resched(cpu);
        }
    }

    irq_spin_give(&sema->spin, key);
    task_switch();
}
#endif

// this function is executed under ISR
static void sema_timeout(task_t * tid, int * expired) {
    // variable `expired` is on the stack of `tid`,
    // which is also protected by `tid->spin`
    u32 key = irq_spin_take(&tid->spin);
    sched_cont(tid, TS_PEND);
    * expired = YES;
    irq_spin_give(&tid->spin, key);
}

// return OK if successfully taken the sema
// return ERROR if failed (might block)
// this function cannot be called inside ISR
int sema_take(sema_t * sema, int timeout) {
    assert(NULL != sema);

    u32 key = irq_spin_take(&sema->spin);

    // check if we can take this sema
    if (sema->value > 0) {
        --sema->value;
        irq_spin_give(&sema->spin, key);
        return OK;
    }

    // if no waiting, just return error
    if (timeout == 0) {
        irq_spin_give(&sema->spin, key);
        return ERROR;
    }

    // resource not available, pend current task
    task_t * tid = thiscpu_var(tid_prev);
    raw_spin_take(&tid->spin);

    // prepare waiter structure
    sem_waiter_t waiter;
    waiter.dl       = DLNODE_INIT;
    waiter.tid      = tid;
    waiter.acquired = NO;
    dl_push_tail(&sema->pend_q, &waiter.dl);

    // prepare timeout watchdog
    int expired = NO;
    wdog_t wd;
    wdog_init(&wd);

    // start wdog while holding `tid->spin`
    // so that timeout will not happen before pending
    if (timeout != WAIT_FOREVER) {
        wdog_start(&wd, timeout, sema_timeout, tid, &expired, 0,0);
    }

    // pend this task
    sched_stop(tid, TS_PEND);
    raw_spin_give(&tid->spin);
    irq_spin_give(&sema->spin, key);
    task_switch();

    // stop the timer and lock sema again
    wdog_cancel(&wd);
    key = irq_spin_take(&sema->spin);

    // check success condition first
    // since timeout might overlap with acquire and delete
    if (YES == waiter.acquired) {
        irq_spin_give(&sema->spin, key);
        return OK;
    }

    // sema not acquired, waiter still in pend_q
    dl_remove(&sema->pend_q, &waiter.dl);

    // TODO: if we have no `freeall`, no signal,
    //       then no need to check `expired`
    //       not acquired just means timeout
#if 0
    // check if we have expired
    if (expired) {
        irq_spin_give(&sema->spin, key);
        return ERROR;
    }
#endif

    // if sema got destroyed
    irq_spin_give(&sema->spin, key);
    return ERROR;
}

// // this function can be called during ISR
// int sema_trytake(sema_t * sema) {
//     u32 key = irq_spin_take(&sema->spin);
//     if (sema->value > 0) {
//         --sema->value;
//         irq_spin_give(&sema->spin, key);
//         return OK;
//     }
//     irq_spin_give(&sema->spin, key);
//     return ERROR;
// }

// this function can be called inside ISR
void sema_give(sema_t * sema) {
    u32 key = irq_spin_take(&sema->spin);

    dlnode_t * dl = dl_pop_head(&sema->pend_q);
    if (NULL == dl) {
        if (sema->value < sema->limit) {
            ++sema->value;
        }
        irq_spin_give(&sema->spin, key);
        return;
    }

    sem_waiter_t * waiter = PARENT(dl, sem_waiter_t, dl);
    task_t * tid = waiter->tid;

    // wake up this task
    raw_spin_take(&tid->spin);
    waiter->acquired = YES;
    sched_cont(tid, TS_PEND);
    int cpu = tid->last_cpu;
    raw_spin_give(&tid->spin);
    irq_spin_give(&sema->spin, key);

    if (cpu_index() == cpu) {
        task_switch();
    } else {
        smp_resched(cpu);
    }
}
