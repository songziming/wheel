#include "bench.h"
#include <task.h>
#include <semaphore.h>
#include <debug.h>


// OS 中运行的测试程序


//------------------------------------------------------------------------------
// 测试信号量，生产者消费者模型
//------------------------------------------------------------------------------

static semaphore_t g_sema;

static void proc_a() {
    while (1) {
        logk("(a-waiting-%d)", cpu_index());
        int got = semaphore_take(&g_sema, 3, FOREVER);
        logk("(a-got-%d)", got);
    }
}

static void proc_b() {
    while (1) {
        logk("(b-waiting-%d)", cpu_index());
        int got = semaphore_take(&g_sema, 2, FOREVER);
        logk("(b-got-%d)", got);
    }
}

static void proc_c() {
    while (1) {
        logk("(c-waiting-%d)", cpu_index());
        int got = semaphore_take(&g_sema, 1, FOREVER);
        logk("(c-got-%d)", got);
    }
}

// 当前任务是生产者，三个高优先级任务是消费者
void bench_semaphore() {
    task_t ta;
    task_t tb;
    task_t tc;
    task_create(&ta, "sema-A", 10, proc_a);
    task_create(&tb, "sema-B", 10, proc_b);
    task_create(&tc, "sema-C", 10, proc_c);

    semaphore_init(&g_sema, 0, 10);

    // 启动三个消费者，开始不断获取资源
    task_start(&ta);
    task_start(&tb);
    task_start(&tc);
    arch_task_switch();

    logk("(root-giving-2)");
    semaphore_give(&g_sema, 2);

    logk("(root-giving-8)");
    semaphore_give(&g_sema, 8);

    logk("\nNow back to root proc\n");
}

