#include <wheel.h>

// a giant lock (tick_q.lock) is used to guard all watch dog operations
// so spinlock is not needed inside wdog_t structure

// `wd->proc == NULL` means this wdog is not in tick_q

typedef struct tick_q {
    spin_t   spin;
    dllist_t q;
} tick_q_t;

static tick_q_t tick_q     = { SPIN_INIT, DLLIST_INIT };
static ssize    tick_count = 0;

void wdog_start(wdog_t * wd, int ticks, void * proc,
                void * a1, void * a2, void * a3, void * a4) {
    assert(NULL != wd);
    assert(NULL != proc);
    assert(ticks >= 0);
    assert(ticks != WAIT_FOREVER);

    u32 key = irq_spin_take(&tick_q.spin);
    if (NULL != wd->proc) {
        irq_spin_give(&tick_q.spin, key);
        return;
    }

    wd->proc = proc;
    wd->arg1 = a1;
    wd->arg2 = a2;
    wd->arg3 = a3;
    wd->arg4 = a4;
    ticks += 1;

    dlnode_t * node = tick_q.q.head;
    wdog_t   * wdog = PARENT(node, wdog_t, node);

    while ((NULL != node) && (wdog->ticks <= ticks)) {
        ticks -= wdog->ticks;
        node   = node->next;
        wdog   = PARENT(node, wdog_t, node);
    }

    wd->ticks = ticks;
    if (wdog) {
        wdog->ticks -= ticks;
    }

    dl_insert_before(&tick_q.q, &wd->node, node);
    irq_spin_give(&tick_q.spin, key);
}

void wdog_stop(wdog_t * wd) {
    u32 key = irq_spin_take(&tick_q.spin);

    if (NULL != wd->proc) {
        dlnode_t * node = wd->node.next;
        wdog_t   * next = PARENT(node, wdog_t, node);
        dl_remove(&tick_q.q, &wd->node);
        wd->proc = NULL;
        if (NULL != node) {
            next->ticks += wd->ticks;
        }
    }

    irq_spin_give(&tick_q.spin, key);
}

// clock interrupt handler
void tick_proc() {
    if (0 == cpu_index()) {
        atomic_inc(&tick_count);

        u32 key = irq_spin_take(&tick_q.spin);
        dlnode_t * node = tick_q.q.head;
        wdog_t   * wdog = PARENT(node, wdog_t, node);
        if (NULL != node) {
            --wdog->ticks;
        }

        while ((NULL != node) && (wdog->ticks <= 0)) {
            dl_pop_head(&tick_q.q);
            wdog_proc_t proc = wdog->proc;
            wdog->proc = NULL;

            // during wdog execution, contention of tick_q is released
            // so we can start (another or the same) wdog inside proc
            irq_spin_give(&tick_q.spin, key);
            proc(wdog->arg1, wdog->arg2, wdog->arg3, wdog->arg4);
            key = irq_spin_take(&tick_q.spin);

            node = tick_q.q.head;
            wdog = PARENT(node, wdog_t, node);
        }

        irq_spin_give(&tick_q.spin, key);
    }

    sched_tick();
}

usize tick_get() {
    return tick_count;
}

// busy wait
void tick_delay(int ticks) {
    usize start = tick_count;
    while ((tick_count - start) < (usize) ticks) {
        cpu_relax();
    }
}
