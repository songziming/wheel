#include <wheel.h>

// P (proberen), a.k.a. take / down
// V (verhogen), a.k.a. give / up

void semaphore_init(semaphore_t * sem, int limit, int value) {
    assert(value <= limit);
    sem->lock   = SPIN_INIT;
    sem->limit  = limit;
    sem->value  = value;
    turnstile_init(&sem->ts);
}

// resume all pending tasks on this semaphore
void semaphore_destroy(semaphore_t * sem) {
    u32 key = irq_spin_take(&sem->lock);

    while (1) {
        task_t * tid = turnstile_pop(&sem->ts);
        if (NULL == tid) {
            break;
        }

        raw_spin_take(&tid->lock);
        sched_cont(tid, TS_PEND);
        // tid->ret_val = ERROR;
        raw_spin_give(&tid->lock);

        if (cpu_index() != tid->last_cpu) {
            smp_reschedule(tid->last_cpu);
        }
    }

    irq_spin_give(&sem->lock, key);
    task_switch();
}

// return OK if successfully taken the semaphore
// return ERROR if failed (might block)
// this function cannot be called inside ISR
int semaphore_take(semaphore_t * sem, int timeout) {
    preempt_lock();
    raw_spin_take(&sem->lock);

    if (sem->value) {
        --sem->value;
        raw_spin_give(&sem->lock);
        preempt_unlock();
        return OK;
    }

    // resource not available, pend current task
    task_t * tid = thiscpu_var(tid_prev);
    raw_spin_take(&tid->lock);

    sched_stop(tid, TS_PEND);
    // tid->ret_val = OK;
    // dl_push_tail(&sem->pend_q, &tid->dl_sched); // TODO: priority-based or FIFO?
    turnstile_push(&sem->ts, tid);

    // wdog_t wd;
    // wdog_init(&wd);

    // // create timeout watchdog
    // if (SEM_WAIT_FOREVER != timeout) {
    //     wdog_start(&wd, timeout, semaphore_timeout, sem, tid, 0,0);
    // }

    // release locks
    raw_spin_give(&tid->lock);
    raw_spin_give(&sem->lock);
    preempt_unlock();

    // pend here
    task_switch();

    // possible reasons for unpending:
    // - successfully taken the semaphore
    // - semaphore got destroyed
    // - timeout

    // cancel watch dog for safety
    // wdog_cancel(&wd);

    // in linux, we have to remove current tid from pend_q if timed out
    // in wheel, tid got auto removed from pend_q during sched_cont
    return OK;
}

// this function can be called inside ISR
void semaphore_give(semaphore_t * sem) {
    u32 key = irq_spin_take(&sem->lock);

    // dlnode_t * dl = dl_pop_head(&sem->pend_q);
    // if (NULL == dl) {
    //     if (sem->count < sem->limit) {
    //         ++sem->count;
    //     }
    //     irq_spin_give(&sem->lock, key);
    //     return;
    // }

    task_t * tid = turnstile_pop(&sem->ts);
    if (NULL == tid) {
        if (sem->value < sem->limit) {
            ++sem->value;
        }
        irq_spin_give(&sem->lock, key);
        return;
    }

    // task_t * tid = PARENT(dl, task_t, dl_sched);
    // int      cpu = tid->last_cpu;
    raw_spin_take(&tid->lock);
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

