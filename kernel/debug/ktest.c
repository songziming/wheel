#include "ktest.h"
#include <task.h>
#include <kstring.h>
#include <debug.h>

#include <apic/apic.h>


//------------------------------------------------------------------------------
// 两个线程相互轮转
//------------------------------------------------------------------------------

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

// called from init code
void test_cooperative() {
    task_create(&ta, "ta", 10, proc_a);
    task_create(&tb, "tb", 10, proc_b);

    // TODO 如果在其他 CPU 运行，启动下一轮测试时会报 #GP
    // TODO 可能原因是 vmspace 没有自旋锁保护，两个 cpu 共同操作页表竞争
    // NEXT 研究 TLB-shootdown 问题，vmspace 加锁
    ta.affinity = 0;
    tb.affinity = 0;

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

//------------------------------------------------------------------------------
// 测试多个 CPU 上同时运行
//------------------------------------------------------------------------------

static task_t smp_tcbs[10];
static char smp_names[10][32];

static void proc_smp() {
    task_t *self = THISCPU_GET(g_tid_prev);

    char tok = 'A';
    for (int i = 0; i < 10; ++i) {
        if (&smp_tcbs[i] == self) {
            tok += i;
            break;
        }
    }

    logk("task-%c running on cpu-%d\n", tok, cpu_index());

    for (int i = 0; i < 100; ++i) {
        logk("(%c%d)", tok, i);
        loapic_timer_busywait(9000);
    }
}

void test_smp_tasks() {
    for (int i = 0; i < 10; ++i) {
        kmemcpy(smp_names[i], "smpX", 5);
        smp_names[i][3] = 'A' + i;
        task_create(&smp_tcbs[i], smp_names[i], 10, proc_smp);
        smp_tcbs[i].affinity = 0;
    }

    int key = cpu_int_lock();
    uint64_t cpuset = 0UL;
    for (int i = 0; i < 10; ++i) {
        cpuset |= task_start(&smp_tcbs[i]);
    }

    // 发送 IPI，触发任务切换
    logk("resched-notify mask %zx\n", cpuset);
    int has_local = 0;
    uint64_t this_mask = 1UL << cpu_index();
    if (cpuset & this_mask) {
        has_local = 1;
        cpuset &= ~this_mask;
    }
    while (cpuset) {
        int cpu = __builtin_ctzll(cpuset);
        cpuset &= cpuset - 1;
        if (cpu_index() == cpu) {
            arch_send_ipi(cpu, VEC_IPI_RESCHED);
        }
    }

    cpu_int_unlock(key);
    if (has_local) {
        arch_task_switch();
    }
}
