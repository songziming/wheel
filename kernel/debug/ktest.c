#include "ktest.h"
#include <task.h>
#include <ktimer.h>
#include <work.h>
#include <semaphore.h>
#include <kstring.h>
#include <debug.h>


// OS 中运行的测试程序


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
    task_create(&t1, "test1", 10, proc_t1);
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

static void proc_a() {
    for (int i = 0; i < 10; ++i) {
        logk("(a-waiting-%d)", cpu_index());
        int got = semaphore_take(&g_sema, 3, FOREVER);
        logk("(a-got-%d)", got);
    }
    logk("sema-a exit\n");
    task_exit();
}

static void proc_b() {
    for (int i = 0; i < 10; ++i) {
        logk("(b-waiting-%d)", cpu_index());
        int got = semaphore_take(&g_sema, 2, FOREVER);
        logk("(b-got-%d)", got);
    }
    logk("sema-b exit\n");
    task_exit();
}

static void proc_c() {
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
    task_create(&ta, "sema-A", 10, proc_a);
    task_create(&tb, "sema-B", 10, proc_b);
    task_create(&tc, "sema-C", 10, proc_c);

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
// 测试调度器：优先级顺序
//------------------------------------------------------------------------------

static task_t g_prio_tasks[3];
static semaphore_t g_prio_sem;

static void prio_worker() {
    task_t *self = THISCPU_GET(g_prev_task);
    logk("(%s running cpu%d)", self->name, cpu_index());
    semaphore_take(&g_prio_sem, 1, FOREVER);
    logk("(%s got semaphore cpu%d)", self->name, cpu_index());
    sched_stop_self(TS_STOPPED);
    arch_task_switch();
}

void test_priority() {
    semaphore_init(&g_prio_sem, 0, 3);

    task_create(&g_prio_tasks[0], "prio-5",  5,  prio_worker);
    task_create(&g_prio_tasks[1], "prio-10", 10, prio_worker);
    task_create(&g_prio_tasks[2], "prio-15", 15, prio_worker);

    preempt_disable();
    for (int i = 0; i < 3; i++) {
        g_prio_tasks[i].affinity = 0;
        task_start(&g_prio_tasks[i]);
    }
    logk("(root yield)");
    arch_task_switch();
    logk("(root back)");

    logk("(root give-1)");
    semaphore_give(&g_prio_sem, 1);
    arch_task_switch();

    logk("(root give-2)");
    semaphore_give(&g_prio_sem, 1);
    arch_task_switch();

    logk("(root give-3)");
    semaphore_give(&g_prio_sem, 1);
    arch_task_switch();

    logk("(priority test done)\n");
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
