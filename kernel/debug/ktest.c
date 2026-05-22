#include "ktest.h"
#include <task.h>
#include <sema.h>
#include <msgq.h>
#include <kstring.h>
#include <debug.h>

#include <apic/apic.h>


//------------------------------------------------------------------------------
// 两个线程相互轮转
//------------------------------------------------------------------------------

static task_t ta;
static task_t tb;

static void proc_a() {
    task_t *self = current_task();
    for (int i = 0; i < 100; ++i) {
        logk("(A%s%d)", self->name, i);
        // THISCPU_SET(g_tid_next, &tb);
        // arch_task_switch();
        if (30 == i) {
            preempt_lock();
        }
        if (60 == i) {
            preempt_unlock();
        }
        loapic_timer_busywait(8000);
    }

    // logk("\nstop at ta\n");
    // while (1) {
    //     cpu_pause();
    //     cpu_halt();
    // }
}

static void proc_b() {
    task_t *self = current_task();
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
    task_t *self = current_task();

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
        // smp_tcbs[i].affinity = 0;
    }

    // 批量启动多个任务，应该禁用中断
    preempt_lock();
    // int key = cpu_int_lock();
    uint64_t cpuset = 0UL;
    for (int i = 0; i < 10; ++i) {
        cpuset |= task_start(&smp_tcbs[i]);
    }
    notify_resched(cpuset);
    preempt_unlock();
    // cpu_int_unlock(key);
    arch_task_switch();

    for (int i = 0; i < 10; ++i) {
        while (TS_DELETED != smp_tcbs[i].state) {
            cpu_pause();
        }
    }
    logk("all smp tasks finished!\n");
}

//------------------------------------------------------------------------------
// 信号量测试
//------------------------------------------------------------------------------

static sema_t g_sem;
static task_t sa;
static task_t sb;
static task_t sc;

static void proc_consumer_a() {
    for (int i = 0; i < 10; ++i) {
        logk("(a-waiting-%d)", cpu_index());
        int got = sema_take(&g_sem, SYSTIMER_FREQ);
        logk("(a-%d)", got);
    }
    logk("sema-a exit\n");
}

static void proc_consumer_b() {
    for (int i = 0; i < 10; ++i) {
        logk("(b-waiting-%d)", cpu_index());
        int got = sema_take(&g_sem, SYSTIMER_FREQ);
        logk("(b-%d)", got);
    }
    logk("sema-b exit\n");
}

static void proc_consumer_c() {
    for (int i = 0; i < 10; ++i) {
        logk("(c-waiting-%d)", cpu_index());
        int got = sema_take(&g_sem, SYSTIMER_FREQ);
        logk("(c-%d)", got);
    }
    logk("sema-c exit\n");
}


void test_sema() {
    task_create(&sa, "sema-A", 10, proc_consumer_a);
    task_create(&sb, "sema-B", 10, proc_consumer_b);
    task_create(&sc, "sema-C", 10, proc_consumer_c);

    sema_init(&g_sem, 0, 1000);

    // 启动三个消费者，开始不断获取资源
    uint64_t cpus = 0;
    preempt_lock();
    cpus |= task_start(&sa);
    cpus |= task_start(&sb);
    cpus |= task_start(&sc);
    notify_resched(cpus);
    preempt_unlock();
    arch_task_switch();

    // 共请求 30 次，提供 28 次，最后两次超时
    for (int i = 0; i < 28; ++i) {
        loapic_timer_busywait(9000);
        sema_give(&g_sem);
    }

    while (TS_DELETED != sa.state) { cpu_pause(); }
    while (TS_DELETED != sb.state) { cpu_pause(); }
    while (TS_DELETED != sc.state) { cpu_pause(); }
    logk("consumers exited\n");
}

//------------------------------------------------------------------------------
// 测试消息队列
//------------------------------------------------------------------------------

static task_t tw;
static task_t tr;
static msgq_t mq;

static void q_writer() {
    logk("writer running\n");

    loapic_timer_busywait(5000);
    logk("writing a...");
    size_t len = msgq_send(&mq, "AAAAA", 5, FOREVER);
    logk("wrote %zu\n", len);

    loapic_timer_busywait(5000);
    logk("writing b...");
    len = msgq_send(&mq, "BBBBB", 5, FOREVER);
    logk("wrote %zu\n", len);

    loapic_timer_busywait(5000);
    msgq_send(&mq, "CCCCC", 5, FOREVER);
}

static void q_reader() {
    logk("reader running\n");
    char dst[1024];
    size_t got = msgq_recv(&mq, dst, 8, FOREVER);
    logk("got %zu bytes: %.*s\n", got, (int)got, dst);
    got = msgq_recv(&mq, dst, 5, FOREVER);
    logk("got %zu bytes: %.*s\n", got, (int)got, dst);
}

void test_msgq() {
    task_create(&tw, "writer", 10, q_writer);
    task_create(&tr, "reader", 10, q_reader);
    tw.affinity = 0;
    tr.affinity = 0;

    msgq_init(&mq);

    preempt_lock();
    task_start(&tw);
    task_start(&tr);
    preempt_unlock();
    arch_task_switch();

    while (TS_DELETED != tw.state) { cpu_pause(); }
    while (TS_DELETED != tr.state) { cpu_pause(); }

    // TODO msgq_destroy
}
