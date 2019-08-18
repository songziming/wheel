#include <wheel.h>

#if (CFG_KERNEL_STACK_SIZE & (CFG_KERNEL_STACK_SIZE - 1)) != 0
    #error "CFG_KERNEL_STACK_SIZE must be power of 2"
#endif

#if CFG_KERNEL_STACK_SIZE < PAGE_SIZE
    #error "CFG_KERNEL_STACK_SIZE must be larger than PAGE_SIZE"
#endif

// create new task
task_t * task_create(int priority, void * proc,
                     void * a1, void * a2, void * a3, void * a4) {
    assert((0 <= priority) && (priority < PRIORITY_COUNT));

    // allocate space for kernel stack, must be a single block
    int   order  = CTZL(CFG_KERNEL_STACK_SIZE) - PAGE_SHIFT;
    pfn_t kstack = page_block_alloc(ZONE_DMA|ZONE_NORMAL, order, PT_KSTACK);
    usize vstack = (usize) phys_to_virt((usize) kstack << PAGE_SHIFT);
    if (NO_PAGE == kstack) {
        return NULL;
    }

    // // mark allocated page as kstack type
    // for (pfn_t i = 0; i < (1U << order); ++i) {
    //     page_array[kstack + i].type  = PT_KSTACK;
    //     page_array[kstack + i].block = 0;
    //     page_array[kstack + i].order = order;
    // }
    // page_array[kstack].block = 1;
    // page_array[kstack].order = order;

    // allocate tcb
    task_t * tid = kmem_alloc(sizeof(task_t));
    if (NULL == tid) {
        page_block_free(kstack, order);
        return NULL;
    }

    // fill register info on the kernel stack
    regs_init(&tid->regs, vstack + CFG_KERNEL_STACK_SIZE, proc, a1, a2, a3, a4);

    // init tcb fields
    tid->spin      = SPIN_INIT;
    tid->dl_sched  = DLNODE_INIT;
    tid->state     = TS_SUSPEND;
    tid->priority  = priority;
    tid->affinity  = 0UL;
    tid->last_cpu  = -1;
    tid->timeslice = CFG_TASK_TIMESLICE;
    tid->remaining = CFG_TASK_TIMESLICE;
    tid->kstack    = kstack;

    return tid;
}

// work function to be called after task_exit during isr
void task_cleanup(task_t * tid) {
    assert(TS_ZOMBIE == tid->state);
    raw_spin_take(&tid->spin);

    // free all pages in kernel stack
    int order = CTZL(CFG_KERNEL_STACK_SIZE) - PAGE_SHIFT;
    assert(page_array[tid->kstack].type  == PT_KSTACK);
    assert(page_array[tid->kstack].order == order);
    assert(page_array[tid->kstack].block == 1);
    page_block_free(tid->kstack, order);

    // release tcb
    kmem_free(sizeof(task_t), tid);
}

// mark current task as deleted
void task_exit() {
    task_t * tid = thiscpu_var(tid_prev);

    u32 key = irq_spin_take(&tid->spin);
    sched_stop(tid, TS_ZOMBIE);
    work_enqueue(task_cleanup, tid, 0,0,0);
    irq_spin_give(&tid->spin, key);

    task_switch();
}

void task_suspend() {
    task_t * tid = thiscpu_var(tid_prev);

    u32 key = irq_spin_take(&tid->spin);
    sched_stop(tid, TS_SUSPEND);
    irq_spin_give(&tid->spin, key);

    task_switch();
}

void task_resume(task_t * tid) {
    assert(NULL != tid);

    u32 key = irq_spin_take(&tid->spin);
    u32 old = sched_cont(tid, TS_SUSPEND);
    int cpu = tid->last_cpu;
    irq_spin_give(&tid->spin, key);

    if (TS_READY == old) {
        return;
    }

    if (cpu_index() == cpu) {
        task_switch();
    } else {
        smp_resched(cpu);
    }
}

void task_delay(int ticks) {
    wdog_t   wd  = WDOG_INIT;
    task_t * tid = thiscpu_var(tid_prev);
    u32      key = irq_spin_take(&tid->spin);
    sched_stop(tid, TS_DELAY);
    wdog_start(&wd, ticks, task_wakeup, tid, 0,0,0);
    irq_spin_give(&tid->spin, key);

    task_switch();
    wdog_stop(&wd);
}

void task_wakeup(task_t * tid) {
    assert(NULL != tid);

    u32 key = irq_spin_take(&tid->spin);
    u32 old = sched_cont(tid, TS_DELAY);
    int cpu = tid->last_cpu;
    irq_spin_give(&tid->spin, key);

    if (TS_READY == old) {
        return;
    }

    if (cpu_index() == cpu) {
        task_switch();
    } else {
        smp_resched(cpu);
    }
}
