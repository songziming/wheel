#include <wheel.h>

// P (proberen), a.k.a. take / down
// V (verhogen), a.k.a. give / up

typedef struct sem_waiter {
    dlnode_t dl;
    task_t * tid;
    int      pri;
    int      up;
} sem_waiter_t;

void semaphore_init(semaphore_t * sem, int limit, int value) {
    assert(value <= limit);
    sem->lock   = SPIN_INIT;
    sem->pend_q = DLLIST_INIT;
    sem->limit  = limit;
    sem->value  = value;
}

// resume all pending tasks on this semaphore
void semaphore_destroy(semaphore_t * sem) {
    u32 key = irq_spin_take(&sem->lock);

    for (dlnode_t * dl = sem->pend_q.head; NULL != dl; dl = dl->next) {
        sem_waiter_t * waiter = PARENT(dl, sem_waiter_t, dl);
        task_t       * tid    = waiter->tid;

        raw_spin_take(&tid->lock);
        sched_cont(tid, TS_PEND);
        raw_spin_give(&tid->lock);

        if (cpu_index() != tid->last_cpu) {
            smp_reschedule(tid->last_cpu);
        }
    }

    irq_spin_give(&sem->lock, key);
    task_switch();
}

// this function is executed under ISR
static void semaphore_timeout(task_t * tid, int * expired) {
    u32 key = irq_spin_take(&tid->lock);
    sched_cont(tid, TS_PEND);
    * expired = YES;
    irq_spin_give(&tid->lock, key);
}

// return OK if successfully taken the semaphore
// return ERROR if failed (might block)
// this function cannot be called inside ISR
int semaphore_take(semaphore_t * sem, int timeout) {
    preempt_lock();
    raw_spin_take(&sem->lock);

    // check if we can take this semaphore
    if (sem->value) {
        --sem->value;
        raw_spin_give(&sem->lock);
        preempt_unlock();
        return OK;
    }


    // resource not available, pend current task
    task_t * tid = thiscpu_var(tid_prev);
    raw_spin_take(&tid->lock);

    // TODO: order by priority?
    sem_waiter_t waiter;
    waiter.dl  = DLNODE_INIT;
    waiter.tid = tid;
    waiter.pri = tid->priority;
    waiter.up  = NO;
    dl_push_tail(&sem->pend_q, &waiter.dl);

    // prepare watchdog timer
    wdog_t wd;
    wdog_init(&wd);
    int expired = NO;
    if (timeout != SEM_WAIT_FOREVER) {
        wdog_start(&wd, timeout, semaphore_timeout, tid, &expired, 0,0);
    }

    while (1) {
        // if (signal_pending) {}

        if (expired) {
            raw_spin_give(&tid->lock);
            raw_spin_give(&sem->lock);
            preempt_unlock();
            return ERROR;
        }

        sched_stop(tid, TS_PEND);

        // release locks and pend here
        raw_spin_give(&tid->lock);
        raw_spin_give(&sem->lock);
        preempt_unlock();
        task_switch();

        // lock semaphore again
        preempt_lock();
        raw_spin_take(&sem->lock);
        raw_spin_take(&tid->lock);

        // whether we've taken the lock
        if (YES == waiter.up) {
            raw_spin_give(&tid->lock);
            raw_spin_give(&sem->lock);
            preempt_unlock();
            return OK;
        }
    }
}

// this function can be called inside ISR
void semaphore_give(semaphore_t * sem) {
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
    waiter->up = YES;
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

