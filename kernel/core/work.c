#include <wheel.h>

typedef void (* work_proc_t) (void * a1, void * a2, void * a3, void * a4);

typedef struct work {
    work_proc_t proc;
    void *      arg1;
    void *      arg2;
    void *      arg3;
    void *      arg4;
} work_t;

// TODO: work queue size should be configurable
#define WORK_Q_SIZE 64

typedef struct work_q {
    spin_t  lock;
    int     count;
    work_t  works[WORK_Q_SIZE];
} work_q_t;

static __PERCPU work_q_t work_q;

// add a new work into the queue on current cpu
void work_enqueue(void * proc, void * a1, void * a2, void * a3, void * a4) {
    work_q_t * q = thiscpu_ptr(work_q);
    u32 key = irq_spin_take(&q->lock);
    if (q->count < WORK_Q_SIZE) {
        q->works[q->count] = (work_t) { proc, a1, a2, a3, a4 };
        ++q->count;
    }
    irq_spin_give(&q->lock, key);
}

// execute all works in the work queue
// usually called during isr, but also during task_switch
void work_dequeue() {
    work_q_t * q = thiscpu_ptr(work_q);
    u32 key = irq_spin_take(&q->lock);
    for (int i = 0; i < q->count; ++i) {
        work_t * w = &q->works[i];
        w->proc(w->arg1, w->arg2, w->arg3, w->arg4);
    }
    q->count = 0;
    irq_spin_give(&q->lock, key);
}

__INIT void work_lib_init() {
    for (int i = 0; i < cpu_installed; ++i) {
        work_q_t * q = percpu_ptr(i, work_q);
        q->lock  = SPIN_INIT;
        q->count = 0;
        memset(q->works, 0, sizeof(work_t) * WORK_Q_SIZE);
    }
}
