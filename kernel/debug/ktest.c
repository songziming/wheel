#include "ktest.h"
#include <task.h>
#include <debug.h>

#include <apic/apic.h>

static task_t ta;
static task_t tb;

static void proc_a() {
    task_t *self = THISCPU_GET(g_tid_prev);
    for (int i = 0; i < 100; ++i) {
        logk("(A%s%d)", self->name, i);
        // THISCPU_SET(g_tid_next, &tb);
        // arch_task_switch();
        loapic_timer_busywait(8000);
    }

    // logk("\nstop at ta\n");
    // while (1) {
    //     cpu_pause();
    //     cpu_halt();
    // }
}

static void proc_b() {
    task_t *self = THISCPU_GET(g_tid_prev);
    for (int i = 0; i < 100; ++i) {
        logk("(B%s%d)", self->name, i);
        // THISCPU_SET(g_tid_next, &ta);
        // arch_task_switch();
        loapic_timer_busywait(8000);
    }

    // logk("\nstop at tb\n");
    // while (1) {
    //     cpu_pause();
    //     cpu_halt();
    // }
}

static int sched_cnt = 0;
void dummy_sched() {
    ++sched_cnt;
    if (sched_cnt & 1) {
        THISCPU_SET(g_tid_next, &ta);
    } else {
        THISCPU_SET(g_tid_next, &tb);
    }
}

// called from init code
void test_cooperative() {
    task_create(&ta, "ta", 10, proc_a);
    task_create(&tb, "tb", 10, proc_b);
    // THISCPU_SET(g_tid_next, &ta);

    int key = cpu_int_lock();
    task_start(&ta);
    task_start(&tb);
    cpu_int_unlock(key);
    arch_task_switch();

    logk("we are back to root");

    while (TS_DELETED != ta.state) {
        cpu_pause();
    }
    while (TS_DELETED != tb.state) {
        cpu_pause();
    }
    logk("TCB safely deleted\n");
}
