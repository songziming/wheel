// 任务调度

#include <task.h>
#include <wheel.h>



// 如果传入 stack_top==NULL，表示动态分配内核栈的物理内存和虚拟范围
// 任务创建失败则返回非零
int task_create_ex(task_t *task, const char *name,
        uint8_t priority, int affinity, context_t *proc,
        void *stack_top, uint8_t stack_rank,
        void *entry, size_t a1, size_t a2, size_t a3, size_t a4) {
    ASSERT(NULL != task);
    ASSERT(priority < PRIORITY_NUM);
    ASSERT(affinity < cpu_count());
    ASSERT(NULL != entry);

    if (NULL == proc) {
        proc = get_kernel_context();
    }

    // 将任务记录在进程中
    task->process = proc;
    // dl_insert_before(&task->proc_node, &proc->proc_head);

    task->spin = SPIN_INIT;
    task->name = name;
    task->state = TASK_STOPPED; // 初始状态为暂停
    task->priority = priority;
    task->affinity = affinity;
    task->last_cpu = affinity;

    task->tick_reload = 10;
    task->tick = 10;
    task->q_node = DLNODE_INIT;

    // 未传入栈指针，需动态分配，动态映射
    if (NULL == stack_top) {
        ASSERT(stack_rank >= 0);

        task->stack = context_alloc(proc, stack_rank, PT_KERNEL_STACK,
                MMU_WRITE, strmake("stack of %s", name));
        if (NULL == task->stack) {
            klog("warning: alloc stack for %s failed\n", name);
            return 1;
        }

        size_t stack_size = PAGE_SIZE << stack_rank;
        memset(task->stack, 0, stack_size);
        stack_top = task->stack + stack_size;
    } else {
        task->stack = NULL;
    }

    // 初始化任务控制块
    size_t args[4] = { a1, a2, a3, a4 };
    arch_tcb_init(&task->arch, (size_t)entry, (size_t)stack_top, args);

    return 0;
}


// 创建简单的内核任务
int task_create(task_t *task, const char *name, uint8_t priority, void *entry) {
    return task_create_ex(task, name, priority, -1, NULL,
            NULL, TASK_STACK_RANK, entry, 0, 0, 0, 0);
}


// 删除任务资源
void task_destroy(task_t *task) {
    ASSERT(NULL != task);

    // 需要确保任务已停止运行，才能释放其内核栈
    // 否则发生中断，访问内核栈会导致 #PF

    int key = irq_spin_take(&task->spin);

    if (NULL != task->stack) {
        context_free(task->process, task->stack);
    }

    irq_spin_give(&task->spin, key);
}



void task_stop(task_t *task) {
    ASSERT(NULL != task);

    int key = irq_spin_take(&task->spin);
    sched_stop(task, TASK_STOPPED);
    irq_spin_give(&task->spin, key);

    // TODO 如果停止的是当前任务，需要立即切换
}

void task_resume(task_t *task) {
    ASSERT(NULL != task);

    // 不能在 sched_cont 内部切换任务，否则自旋锁无法释放
    int key = irq_spin_take(&task->spin);
    sched_cont(task, TASK_STOPPED);
    irq_spin_give(&task->spin, key);

    // TODO 判断有没有抢占，从而决定是否切换任务
}




static void task_wakeup(void *arg) {
    task_t *task = (task_t *)arg;

    int key = irq_spin_take(&task->spin);
    sched_cont(task, TASK_STOPPED);
    irq_spin_give(&task->spin, key);

    // 不需要切换任务，我们已经处于中断
}

void task_delay(int ticks) {
    task_t *self = THISCPU_GET(g_tid_prev);

    int key = irq_spin_take(&self->spin);
    sched_cont(self, TASK_STOPPED);
    irq_spin_give(&self->spin, key);

    work_t wait_work = WORK_INIT;
    tick_delay(&wait_work, ticks, task_wakeup, self);
    arch_task_switch();
}



// 退出当前任务
void task_exit() {
    task_t *self = THISCPU_GET(g_tid_prev);

    // 将任务标记为已删除（当前任务一定处于就绪态）
    int key = irq_spin_take(&self->spin);
    sched_stop(self, TASK_DELETED);
    irq_spin_give(&self->spin, key);

    // 当前任务正在运行，不能此时删除 TCB
    // 下一次中断，任务已停止执行，才能删除任务
    work_t exit_work = WORK_INIT;
    work_defer(&exit_work, (work_func_t)task_destroy, self);

    // 立即切换到新任务
    arch_task_switch();
}
