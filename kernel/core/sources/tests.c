// 在 OS 中运行的压力测试

#include <wheel.h>
#include <shell.h>

#include <semaphore.h>



//------------------------------------------------------------------------------
// 测试 IPC - 生产者消费者
//------------------------------------------------------------------------------

static semaphore_t g_sem;

static void func_producer() {
    //
}

static void func_consumer() {
    //
}

static int test_ipc(UNUSED int argc, UNUSED char *argv[]) {
    semaphore_init(&g_sem, 10, 10);

    task_t p, c;
    task_create(&p, "producer", 10, func_producer);
    task_create(&c, "consumer", 10, func_consumer);

    // 当前函数运行在 shell 任务中，处于最高优先级
    // 将当前任务阻塞在这里

    return 0;
}



//------------------------------------------------------------------------------
// 添加测试命令，可以在 kernel shell 里启动
//------------------------------------------------------------------------------

static shell_cmd_t g_cmd_sched;

void tests_init() {
    g_cmd_sched.name = "test-sched";
    g_cmd_sched.func = test_ipc;
    shell_add_cmd(&g_cmd_sched);
}
