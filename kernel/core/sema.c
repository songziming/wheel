#include <wheel.h>

// P (proberen), a.k.a. take / down
// V (verhogen), a.k.a. give / up

typedef struct pender {
    dlnode_t dl;
    task_t * tid;
    int      up;
} pender_t;

// this function is executed under ISR
static void sema_timeout(task_t * tid, int * expired) {
    assert(NULL != tid);
    assert(NULL != expired);

    u32 key = irq_spin_take(&tid->spin);
    sched_cont(tid, TS_PEND);
    * expired = YES;
    irq_spin_give(&tid->spin, key);
}

// return OK for success, ERROR for fail
int sema_take(sema_t * sema, int timeout) {
    assert(NULL != sema);

    u32 key = irq_spin_take(&sema->spin);
    if (sema->value > 0) {
        --sema->value;
        irq_spin_give(&sema->spin, key);
        return OK;
    }
    if (NO_WAIT == timeout) {
        irq_spin_give(&sema->spin, key);
        return ERROR;
    }

    // resource not available, pend current task
    wdog_t   wd      = WDOG_INIT;
    int      expired = NO;
    task_t * tid     = thiscpu_var(tid_prev);
    pender_t pender  = {
        .dl  = DLNODE_INIT,
        .tid = tid,
        .up  = NO,
    };

    dl_push_tail(&sema->pend_q, &pender.dl);

    // start wdog while holding `tid->spin`
    // so that timeout will not happen before pending
    raw_spin_take(&tid->spin);
    if (timeout != WAIT_FOREVER) {
        wdog_start(&wd, timeout, sema_timeout, tid, &expired, 0,0);
    }

    // pend here
    sched_stop(tid, TS_PEND);
    raw_spin_give(&tid->spin);
    irq_spin_give(&sema->spin, key);
    task_switch();

    // on wakeup, stop timer and lock semaphore again
    wdog_stop(&wd);
    key = irq_spin_take(&sema->spin);

    if (YES == pender.up) {
        irq_spin_give(&sema->spin, key);
        return OK;
    }
    if (YES == expired) {
        dl_remove(&sema->pend_q, &pender.dl);
        irq_spin_give(&sema->spin, key);
        return ERROR;
    }

    panic("semaphore wakeup exception\n");
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

    pender_t * pender = PARENT(dl, pender_t, dl);
    task_t   * tid    = pender->tid;

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
