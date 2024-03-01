#include <wheel.h>



// TODO 平台相关函数，应该替换成 tick_wait
INIT_TEXT void local_apic_busywait(int us);



//------------------------------------------------------------------------------
// 测试自旋锁
//------------------------------------------------------------------------------

static spin_t g_test_spin;
static volatile int g_test_val;
static task_t g_task_a;
static task_t g_task_b;

static void spin_test_proc() {
    klog("running on cpu %d\n", cpu_index());
    while (1) {
        raw_spin_take(&g_test_spin);
        int old = __sync_fetch_and_add(&g_test_val, 1);
        if (0 != old) {
            klog("spinlock error!\n");
            while (1) {
                cpu_halt();
            }
        }
        local_apic_busywait(100);
        g_test_val= 0;
        raw_spin_give(&g_test_spin);
    }
}

void test_spin_lock() {
    g_test_spin = SPIN_INIT;
    g_test_val = 0;
    task_create(&g_task_a, "spin-a", 1, spin_test_proc);
    task_create(&g_task_b, "spin-b", 1, spin_test_proc);
    g_task_a.affinity = 1;
    g_task_b.affinity = 2;
    task_resume(&g_task_a);
    task_resume(&g_task_b);
}



//------------------------------------------------------------------------------
// 测试块设备读写
//------------------------------------------------------------------------------

// TODO 注册两个读写扇区的 shell 命令

void test_block_io() {
    blk_dev_t *dev = get_block_device();

    if (NULL == dev) {
        klog("no block device!\n");
        return;
    }

    uint8_t *sec = kernel_heap_alloc(dev->sec_size);
    if (NULL == sec) {
        klog("cannot alloc space for sector!\n");
        return;
    }

    block_read(dev, sec, 0, 1);
    klog("sector 0:");
    for (int i = 0; i < 10; ++i) {
        klog(" %02x", sec[i]);
        sec[i] = i * 16;
    }
    klog(".\n");

    block_write(dev, sec, 0, 1);

    kernel_heap_free(sec);
}



//------------------------------------------------------------------------------
// 测试任务调度
//------------------------------------------------------------------------------

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
// 运行在 BSP，此时 AP 均已启动，正在运行 idle task
// 启动平台无关的系统服务（文件系统、调试日志、keyboard 中间件、内核 shell）
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
