#include <wheel.h>



// TODO 平台相关函数，应该替换成 tick_wait
INIT_TEXT void local_apic_busywait(int us);


static void task_a_proc() {
    while (1) {
        klog("A");
        local_apic_busywait(1000000);
    }
}

static void task_b_proc() {
    while (1) {
        klog("B");
        local_apic_busywait(1000000);
    }
}

// 平台无关的初始化流程，在根任务中调用
// TODO 改名为 sys_init
INIT_TEXT void common_init() {
    task_t a_tcb;
    task_t b_tcb;

    task_create(&a_tcb, "testA", 10, task_a_proc);
    task_create(&b_tcb, "testB", 9, task_b_proc);

    task_resume(&a_tcb);
    task_resume(&b_tcb);

    task_t *self = THISCPU_GET(g_tid_prev);
    task_stop(self);

    klog("root task resumed\n");
}
