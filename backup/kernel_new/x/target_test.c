#include "target_test.h"
#include <arch_intf.h>

#include <library/debug.h>
#include <proc/tick.h>
#include <proc/sched.h>
#include <x/semaphore.h>


// 一些在 OS 中执行的测试用例
// root task 最后执行


//------------------------------------------------------------------------------
// 定时器周期性触发
//------------------------------------------------------------------------------

static timer_t wd;

static void wd_func(void *a1 UNUSED, void *a2 UNUSED) {
    static int cnt = 0;
    log("watchdog-%d\n", cnt++);

    timer_start(&wd, 20, (timer_func_t)wd_func, 0, 0);
}

void test_timer_periodic() {
    timer_start(&wd, 20, (timer_func_t)wd_func, 0, 0);
}


//------------------------------------------------------------------------------
// 测试休眠
//------------------------------------------------------------------------------

void loapic_timer_busywait(int us); // loapic

void test_task_sleep() {
    for (int i = 0; i < 100; ++i) {
        log("iteration #%d\n", i);
        task_sleep(80);
        // loapic_timer_busywait(1000000);
    }
}

//------------------------------------------------------------------------------
// 测试信号量
//------------------------------------------------------------------------------

static semaphore_t g_sem;
static task_t g_producer;
static task_t g_consumer;

static void proc_p() {
    log("producer on cpu-%d\n", cpu_index());

    while (1) {
        log("(+5)");
        semaphore_give(&g_sem, 5);
        task_sleep(5);
    }
}

static void proc_c() {
    log("consumer on cpu-%d\n", cpu_index());

    while (1) {
        int got = semaphore_take(&g_sem, 7, FOREVER);
        log("<-%d>", got);
        // task_sleep(10);
    }
}

void test_producer_consumer() {
    semaphore_init(&g_sem, 0, 10);
    task_create(&g_producer, "producer", 0, 10, proc_p, 0,0,0,0);
    task_create(&g_consumer, "consumer", 0, 10, proc_c, 0,0,0,0);
    task_resume(&g_producer);
    task_resume(&g_consumer);
}
