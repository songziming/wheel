#include <wheel.h>

typedef struct tick_q {
    spin_t   lock;
    dllist_t q;
} tick_q_t;

static tick_q_t tick_q     = { SPIN_INIT, DLLIST_INIT };
static ssize    tick_count = 0;

void wdog_init(wdog_t * dog) {
    memset(dog, 0, sizeof(wdog_t));
    dog->node.prev = &dog->node;
    dog->node.next = &dog->node;
}

void wdog_start(wdog_t * dog, int ticks, void * proc,
                void * a1, void * a2, void * a3, void * a4) {
    assert(NULL != dog);
    assert(ticks >= 0);

    if ((dog->node.prev != &dog->node) && (dog->node.next != &dog->node)) {
        return;
    }

    dog->proc = proc;
    dog->arg1 = a1;
    dog->arg2 = a2;
    dog->arg3 = a3;
    dog->arg4 = a4;
    ticks += 1;

    u32 key = irq_spin_take(&tick_q.lock);
    dlnode_t * node = tick_q.q.head;
    wdog_t   * wdog = PARENT(node, wdog_t, node);

    while ((NULL != node) && (wdog->ticks <= ticks)) {
        ticks -= wdog->ticks;
        node   = node->next;
        wdog   = PARENT(node, wdog_t, node);
    }

    dog->ticks = ticks;
    if (wdog) {
        wdog->ticks -= ticks;
    }

    dl_insert_before(&tick_q.q, &dog->node, node);
    irq_spin_give(&tick_q.lock, key);
}

void wdog_cancel(wdog_t * dog) {
    u32 key = irq_spin_take(&tick_q.lock);
    if ((dog->node.prev != &dog->node) && (dog->node.next != &dog->node)) {
        dlnode_t * node = dog->node.next;
        wdog_t   * next = PARENT(node, wdog_t, node);
        dl_remove(&tick_q.q, &dog->node);
        dog->node.prev = &dog->node;
        dog->node.next = &dog->node;
        if (NULL != node) {
            next->ticks += dog->ticks;
        }
    }
    irq_spin_give(&tick_q.lock, key);
}

// clock interrupt handler
void tick_proc() {
    if (0 == cpu_index()) {
        atomic_inc(&tick_count);

        u32 k = irq_spin_take(&tick_q.lock);
        dlnode_t * node = tick_q.q.head;
        wdog_t   * dog  = PARENT(node, wdog_t, node);
        if (NULL != node) {
            --dog->ticks;
        }

        while ((NULL != node) && (dog->ticks <= 0)) {
            dl_pop_head(&tick_q.q);
            dog->node.prev = &dog->node;
            dog->node.next = &dog->node;

            irq_spin_give(&tick_q.lock, k);
            dog->proc(dog->arg1, dog->arg2, dog->arg3, dog->arg4);
            k = irq_spin_take(&tick_q.lock);

            node = tick_q.q.head;
            dog  = PARENT(node, wdog_t, node);
        }

        irq_spin_give(&tick_q.lock, k);
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
