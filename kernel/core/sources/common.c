#include <wheel.h>



// TODO 平台相关函数，应该替换成 tick_wait
INIT_TEXT void local_apic_busywait(int us);


static void task_a_proc() {
    for (int i = 0; i < 15; ++i) {
        klog("A");
        local_apic_busywait(400000);
    }
}

static void task_b_proc() {
    while (1) {
        klog("B");
        local_apic_busywait(400000);
    }
}


static void my_work(void *arg) {
    klog("[executing watch dog %s]\n", (const char *)arg);
}

static work_t wa, wb;


// 平台无关的初始化流程，在根任务中调用
// 启动平台无关的系统服务（文件系统、调试日志）
INIT_TEXT void common_init() {
    task_t a_tcb;
    task_t b_tcb;

    task_create(&a_tcb, "testA", 10, task_a_proc);
    task_create(&b_tcb, "testB", 10, task_b_proc);

    task_resume(&a_tcb);
    task_resume(&b_tcb);

    tick_delay(&wa, 20, my_work, "WORK_20");
    tick_delay(&wb, 40, my_work, "WORK_40");

    task_t *self = THISCPU_GET(g_tid_prev);
    task_stop(self);

    klog("force switch task\n");
    arch_task_switch();

    klog("root task resumed\n");
}
