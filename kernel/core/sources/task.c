// 任务调度

#include <task.h>
#include <wheel.h>






// 如果传入 stack_top==NULL，表示动态分配内核栈的物理内存和虚拟范围
int task_create_ex(task_t *task, const char *name,
        uint8_t priority, int affinity, process_t *proc,
        void *stack_top, uint8_t stack_rank,
        void *entry, size_t a1, size_t a2, size_t a3, size_t a4) {
    ASSERT(NULL != task);
    ASSERT(priority < PRIORITY_NUM);
    ASSERT(affinity < cpu_count());
    ASSERT(NULL != entry);

    if (NULL == proc) {
        proc = get_kernel_process();
    }

    // 未传入栈指针，需动态分配，动态映射
    if (NULL == stack_top) {
        vmspace_t *kernel_vm = get_kernel_vmspace();
        size_t kernel_pg = get_kernel_pgtable();

        size_t stack_size = PAGE_SIZE << stack_rank;
        size_t va = vmspace_search(kernel_vm, STACK_AREA_ADDR, STACK_AREA_END, stack_size);
        if (INVALID_ADDR == va) {
            klog("cannot reserve range for task %s\n", name);
            return 1;
        }

        // 为内核栈申请物理内存
        size_t pa = pages_alloc(stack_rank, PT_KERNEL_STACK);
        if (INVALID_ADDR == pa) {
            klog("cannot alloc page for task %s\n", name);
            return 1;
        }

        // 建立内核栈的映射
        vmspace_insert(kernel_vm, &task->stack_va, va, va + stack_size, name);
        mmu_map(kernel_pg, va, va + stack_size, pa, MMU_WRITE);
        kmemset((char *)va, 0, stack_size);

        task->stack_pa = pa;
        stack_top = (void *)(va + stack_size);
    } else {
        task->stack_pa = INVALID_ADDR;
    }

    // 将任务记录在进程中
    task->process = proc;
    dl_insert_before(&task->proc_node, &proc->proc_head);

    // 初始化任务控制块
    size_t args[4] = { a1, a2, a3, a4 };
    arch_tcb_init(&task->arch, (size_t)entry, (size_t)stack_top, args);

    task->spin = SPIN_INIT;
    task->name = name;
    task->state = TASK_STOPPED; // 初始状态为暂停
    task->priority = priority;
    task->affinity = affinity;

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

    if (INVALID_ADDR != task->stack_pa) {
        vmspace_t *kernel_vm = get_kernel_vmspace();
        size_t kernel_pg = get_kernel_pgtable();

        mmu_unmap(kernel_pg, task->stack_va.addr, task->stack_va.end);
        vmspace_remove(kernel_vm, &task->stack_va);
    }

    irq_spin_give(&task->spin, key);
}



void task_stop(task_t *task) {
    ASSERT(NULL != task);

    int key = irq_spin_take(&task->spin);
    sched_stop(task, TASK_STOPPED);
    irq_spin_give(&task->spin, key);
}

void task_resume(task_t *task) {
    ASSERT(NULL != task);

    int key = irq_spin_take(&task->spin);
    sched_cont(task, TASK_STOPPED);
    irq_spin_give(&task->spin, key);
}

// 退出当前任务
void task_exit() {
    task_t *self = THISCPU_GET(g_tid_prev);

    int key = irq_spin_take(&self->spin);
    sched_stop(self, TASK_DELETED);
    // TODO 还要将任务从就绪队列或等待队列中移除
    irq_spin_give(&self->spin, key);

    // 立即切换，顺便在 work_q 中彻底删除任务
    klog("yielding from %s(%p) to %s(%p)\n",
            THISCPU_GET(g_tid_prev)->name, THISCPU_GET(g_tid_prev),
            THISCPU_GET(g_tid_next)->name, THISCPU_GET(g_tid_next));
    arch_task_yield();
}
