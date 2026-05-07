#include "ktest.h"
#include <task.h>
#include <ktimer.h>
#include <work.h>
#include <semaphore.h>
#include <kstring.h>
#include <debug.h>

#include <arch_int.h>


// OS 中运行的测试程序

//------------------------------------------------------------------------------
// 测试两个任务来回切换，主动切换
//------------------------------------------------------------------------------

// static task_t *task_root;
static task_t task_ping;
static task_t task_pong;

static void proc_ping() {
    logk("(task ping running)");
    // logk("[pong regs %p rsp %zx\n", task_pong.regs, ((regs_t*)task_pong.regs)->rsp);
    for (int i = 0; i < 60; ++i) {
        if (i % 10 == 0) {
            size_t rsp;
            ASMV("movq %%rsp,%0" : "=r"(rsp));
            logk("(ping-%d-%zx)", i, rsp);
        } else {
            logk("(ping-%d)", i);
        }
        THISCPU_SET(g_next_task, &task_pong);
        arch_task_switch();
    }
    logk("(task ping finished)");
    // THISCPU_SET(g_next_task, &task_pong);
    // arch_task_switch();
}

static void proc_pong() {
    logk("(task pong running)");
    // logk("[ping regs %p rsp %zx\n", task_ping.regs, ((regs_t*)task_ping.regs)->rsp);
    for (int i = 0; i < 60; ++i) {
        if (i % 10 == 0) {
            size_t rsp;
            ASMV("movq %%rsp,%0" : "=r"(rsp));
            logk("(pong-%d-%zx)", i, rsp);
        } else {
            logk("(pong-%d)", i);
        }
        THISCPU_SET(g_next_task, &task_ping);
        arch_task_switch();
    }
    logk("(task pong finished)");
    // THISCPU_SET(g_next_task, task_root);
    // arch_task_switch();
}

void test_pingpong() {
    ASMV("int $0xd0"); // resched

    // task_root = THISCPU_GET(g_prev_task);
    task_create(&task_ping, "ping", 10, proc_ping);
    // logk("ping regs %p rsp %zx\n", task_ping.regs, ((regs_t*)task_ping.regs)->rsp);
    task_create(&task_pong, "pong", 10, proc_pong);
    // logk("pong regs %p rsp %zx\n", task_pong.regs, ((regs_t*)task_pong.regs)->rsp);
    task_ping.affinity = 0;
    task_pong.affinity = 0;


    preempt_disable();
    task_start(&task_ping);
    task_start(&task_pong);
    // logk("ping regs %p rsp %zx\n", task_ping.regs, ((regs_t*)task_ping.regs)->rsp);
    // logk("pong regs %p rsp %zx\n", task_pong.regs, ((regs_t*)task_pong.regs)->rsp);
    THISCPU_SET(g_next_task, &task_ping);
    // logk("ping regs %p rsp %zx\n", task_ping.regs, ((regs_t*)task_ping.regs)->rsp);
    logk(">>> testing pingpong\n");
    arch_task_switch();
    logk("(back to root)\n");
}

//------------------------------------------------------------------------------
// 测试调度器：优先级抢占
//------------------------------------------------------------------------------

static task_t task_a;
static task_t task_b;
static task_t task_c;

static void proc_a() {
    int i = 0;
    for (; i < 10; ++i) {
        logk("(A-%d)", i);
        arch_task_switch();
    }
    task_start(&task_b);
    arch_task_switch();
    for (; i < 20; ++i) {
        logk("(A-%d)", i);
        arch_task_switch();
    }
    logk("(A-exit)");
}

static void proc_b() {
    int i = 0;
    for (; i < 10; ++i) {
        logk("(B-%d)", i);
        arch_task_switch();
    }
    task_start(&task_c);
    arch_task_switch();
    for (; i < 20; ++i) {
        logk("(B-%d)", i);
        arch_task_switch();
    }
    logk("(B-exit)");
}

static void proc_c() {
    for (int i = 0; i < 10; ++i) {
        logk("(C-%d)", i);
        arch_task_switch();
    }
}

void test_priority() {
    task_create(&task_a, "ta", 9, proc_a);
    task_create(&task_b, "tb", 8, proc_b);
    task_create(&task_c, "tc", 7, proc_c);
    task_a.affinity = 0;
    task_b.affinity = 0;
    task_c.affinity = 0;

    logk(">>> testing priority\n");
    task_start(&task_a);
    arch_task_switch();
    logk("(priority test done)\n");
}

//------------------------------------------------------------------------------
// 测试任务创建和退出
//------------------------------------------------------------------------------

static task_t t1;

static void proc_t1() {
    logk("test-task-1 created\n");
    task_exit();
    logk("test-task-1 is still running\n");
}

void test_enterleave() {
    task_create(&t1, "t1", 10, proc_t1);
    task_start(&t1);
    arch_task_switch();

    logk("back from sub task\n");
}

//------------------------------------------------------------------------------
// 测试信号量，生产者消费者模型
//------------------------------------------------------------------------------

static task_t ta;
static task_t tb;
static task_t tc;
static semaphore_t g_sema;

static void proc_consumer_a() {
    for (int i = 0; i < 10; ++i) {
        logk("(a-waiting-%d)", cpu_index());
        int got = semaphore_take(&g_sema, 3, FOREVER);
        logk("(a-got-%d)", got);
    }
    logk("sema-a exit\n");
    task_exit();
}

static void proc_consumer_b() {
    for (int i = 0; i < 10; ++i) {
        logk("(b-waiting-%d)", cpu_index());
        int got = semaphore_take(&g_sema, 2, FOREVER);
        logk("(b-got-%d)", got);
    }
    logk("sema-b exit\n");
    task_exit();
}

static void proc_consumer_c() {
    for (int i = 0; i < 10; ++i) {
        logk("(c-waiting-%d)", cpu_index());
        int got = semaphore_take(&g_sema, 1, FOREVER);
        logk("(c-got-%d)", got);
    }
    logk("sema-c exit\n");
    task_exit();
}

// 当前任务是生产者，三个高优先级任务是消费者
void test_semaphore() {
    task_create(&ta, "sema-A", 10, proc_consumer_a);
    task_create(&tb, "sema-B", 10, proc_consumer_b);
    task_create(&tc, "sema-C", 10, proc_consumer_c);

    semaphore_init(&g_sema, 0, 1000);

    // 启动三个消费者，开始不断获取资源
    preempt_disable();
    task_start(&ta);
    task_start(&tb);
    task_start(&tc);
    arch_task_switch();

    logk("(root-giving-2)");
    semaphore_give(&g_sema, 2);

    logk("(root-giving-8)");
    semaphore_give(&g_sema, 8);

    logk("\nNow back to root proc\n");
    // TODO 需要将三个 task_t 释放
    semaphore_give(&g_sema, 1000);
}


//------------------------------------------------------------------------------
// 测试调度器：同优先级轮转
//------------------------------------------------------------------------------

#define RR_COUNT 4
#define RR_LOOPS 3

static task_t g_rr_tasks[RR_COUNT];

static void rr_worker() {
    logk("rr-task starting on cpu-%d\n", cpu_index());
    task_t *self = THISCPU_GET(g_prev_task);
    int id = -1;
    for (int i = 0; i < RR_COUNT; i++) {
        if (self == &g_rr_tasks[i]) { id = i; break; }
    }

    for (int loop = 0; loop < RR_LOOPS; loop++) {
        logk("(rr-%d:%d cpu%d)", id, loop, cpu_index());
        task_t *t = sched_stop_self(TS_PENDING);
        sched_cont(t, TS_PENDING);
        arch_task_switch();
    }

    logk("rr-%d stopped on cpu-%d\n", id, cpu_index());
    sched_stop_self(TS_STOPPED);
    arch_task_switch();
}

void test_round_robin() {
    kmemset(g_rr_tasks, 0, sizeof(g_rr_tasks));

    for (int i = 0; i < RR_COUNT; i++) {
        task_create(&g_rr_tasks[i], "rr", 10, rr_worker);
        g_rr_tasks[i].affinity = 0;
    }

    preempt_disable();
    for (int i = 0; i < RR_COUNT; i++) {
        task_start(&g_rr_tasks[i]);
    }

    // rr-tasks 优先级高于 root task，需要在 work 里面启动
    // 否则第一个任务就会切换过去
    // ktimer_t start_rr;
    // timer_start(&start_rr, start_rr_tasks, 0);
    // work_t start_rr;
    // work_defer(&start_rr, start_rr_tasks, "start rr tasks");
    logk("(root yielding to rr-tasks)");
    arch_task_switch();
    logk("(root back from rr)\n");
}


//------------------------------------------------------------------------------
// 测试调度器：大量任务压力测试
//------------------------------------------------------------------------------

#define STRESS_COUNT 20
#define STRESS_LOOPS 5

static task_t g_stress_tasks[STRESS_COUNT];

static void stress_worker() {
    task_t *self = THISCPU_GET(g_prev_task);
    int id = -1;
    for (int i = 0; i < STRESS_COUNT; i++) {
        if (self == &g_stress_tasks[i]) { id = i; break; }
    }

    for (int loop = 0; loop < STRESS_LOOPS; loop++) {
        logk("(s%d.%d cpu%d)", id, loop, cpu_index());
        task_t *t = sched_stop_self(TS_PENDING);
        sched_cont(t, TS_PENDING);
        arch_task_switch();
    }

    sched_stop_self(TS_STOPPED);
    arch_task_switch();
}

void test_sched_stress() {
    for (int i = 0; i < STRESS_COUNT; i++) {
        int prio = 10 + (i % 5);
        task_create(&g_stress_tasks[i], "stress", prio, stress_worker);
        g_stress_tasks[i].affinity = 0;
    }
    preempt_disable();
    for (int i = 0; i < STRESS_COUNT; i++) {
        task_start(&g_stress_tasks[i]);
    }
    logk("(root yielding for stress test)");
    arch_task_switch();
    logk("(root back from stress)\n");
}


//------------------------------------------------------------------------------
// 测试内存池分配器
//------------------------------------------------------------------------------

// TBD
