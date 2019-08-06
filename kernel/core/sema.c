#include <wheel.h>

// P (proberen), a.k.a. take / down
// V (verhogen), a.k.a. give / up

typedef struct sem_pender {
    dlnode_t dl;
    task_t * tid;
    int      up;
} sema_pender_t;

void sema_init(sema_t * sema, int limit, int value) {
    assert(value <= limit);
    sema->spin   = SPIN_INIT;
    sema->pend_q = DLLIST_INIT;
    sema->limit  = limit;
    sema->value  = value;
}

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
    if (NO_WAIT == timeout) {
        irq_spin_give(&sema->spin, key);
        return ERROR;
    }

    // resource not available, pend current task
    task_t * tid = thiscpu_var(tid_prev);
    raw_spin_take(&tid->spin);

    // prepare pender structure
    sema_pender_t pender = {
        .dl  = DLNODE_INIT,
        .tid = tid,
        .up  = NO,
    };
    dl_push_tail(&sema->pend_q, &pender.dl);

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
    if (YES == pender.up) {
        irq_spin_give(&sema->spin, key);
        return OK;
    }

    // sema not acquired, pender still in pend_q
    assert(YES == expired);
    dl_remove(&sema->pend_q, &pender.dl);
    irq_spin_give(&sema->spin, key);
    return ERROR;
}

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

    sema_pender_t * pender = PARENT(dl, sema_pender_t, dl);
    task_t * tid = pender->tid;

    // wake up this task
    raw_spin_take(&tid->spin);
    pender->up = YES;
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
