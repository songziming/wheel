#include <wheel.h>

typedef void (* work_proc_t) (void * a1, void * a2, void * a3, void * a4);

typedef struct work {
    work_proc_t proc;
    void *      arg1;
    void *      arg2;
    void *      arg3;
    void *      arg4;
} work_t;

typedef struct work_q {
    spin_t  spin;
    int     count;
    work_t  works[CFG_WORK_QUEUE_SIZE];
} work_q_t;

static __PERCPU work_q_t work_q;

#ifdef DEBUG
static int lib_initialized = NO;
#endif

// add a new work into the queue on current cpu
void work_enqueue(void * proc, void * a1, void * a2, void * a3, void * a4) {
#ifdef DEBUG
    if (YES != lib_initialized) {
        panic("work lib not initialized.\n");
    }
#endif

    work_q_t * q = thiscpu_ptr(work_q);
    u32 key = irq_spin_take(&q->spin);
    if (q->count < CFG_WORK_QUEUE_SIZE) {
        q->works[q->count] = (work_t) { proc, a1, a2, a3, a4 };
        ++q->count;
    }
    irq_spin_give(&q->spin, key);
}

// execute all works in the work queue
// usually called during isr, but also during task_switch
void work_dequeue() {
#ifdef DEBUG
    if (YES != lib_initialized) {
        panic("work lib not initialized.\n");
    }
#endif

    work_q_t * q = thiscpu_ptr(work_q);
    u32 key = irq_spin_take(&q->spin);
    for (int i = 0; i < q->count; ++i) {
        work_t * w = &q->works[i];
        w->proc(w->arg1, w->arg2, w->arg3, w->arg4);
    }
    q->count = 0;
    irq_spin_give(&q->spin, key);
}

__INIT void work_lib_init() {
#ifdef DEBUG
    if (YES == lib_initialized) {
        panic("work lib already initialized.\n");
    }
    lib_initialized = YES;
#endif

    for (int i = 0; i < cpu_installed; ++i) {
        work_q_t * q = percpu_ptr(i, work_q);
        q->spin  = SPIN_INIT;
        q->count = 0;
        memset(q->works, 0, sizeof(work_t) * CFG_WORK_QUEUE_SIZE);
    }
}
