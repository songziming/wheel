// 任务调度

#include <task.h>
#include <wheel.h>
#include <page.h>
#include <vmspace.h>
#include <str.h>



PCPU_BSS task_t *g_tid_prev;  // 当前正在运行的任务
PCPU_BSS task_t *g_tid_next;  // 下次中断将要切换的任务

// 就绪队列
static PCPU_BSS dlnode_t ready_head;

// 空闲任务
static PCPU_BSS task_t idle_task;



// void task_create_ex(task_t *task, const char *name, void *entry, process_t *proc,
//         int stack_rank, size_t args[4]) {
//     //
// }

// 任务创建失败则返回非零
rc_t task_create(task_t *task, const char *name, void *entry, process_t *proc) {
    ASSERT(NULL != task);
    ASSERT(NULL != entry);

    if (NULL == proc) {
        proc = get_kernel_process();
    }

    vmspace_t *kernel_vm = get_kernel_vmspace();
    size_t kernel_pg = get_kernel_pgtable();

    // 为内核栈寻找一段虚拟地址范围
    // TODO 内核栈大小固定，可以按编号计算分配
    size_t stack_size = PAGE_SIZE << TASK_STACK_RANK;
    size_t va = vmspace_search(kernel_vm, STACK_AREA_ADDR, STACK_AREA_END, stack_size);
    if (INVALID_ADDR == va) {
        klog("cannot reserve range for task %s\n", name);
        return RC_NO_FREE_RANGE;
    }

    // 为内核栈申请物理内存
    size_t pa = pages_alloc(TASK_STACK_RANK, PT_KERNEL_STACK);
    if (INVALID_ADDR == pa) {
        klog("cannot alloc page for task %s\n", name);
        return RC_NO_FREE_PAGE;
    }

    klog("task '%s' stack %zx --> %zx\n", name, va, pa);

    // 建立内核栈的映射
    vmspace_insert(kernel_vm, &task->stack_va, va, va + stack_size, name);
    mmu_map(kernel_pg, va, va + stack_size, pa, MMU_WRITE);
    kmemset((char *)va, 0, stack_size);

    // 将任务记录在进程中
    dl_insert_before(&task->proc_node, &proc->proc_head);

    arch_tcb_init(&task->arch, entry, va + stack_size);
    task->stack_pa = pa;
    task->name = name;
    task->process = proc;
    return RC_OK;
}




// 将任务放入就绪队列
void task_resume(task_t *task) {
    ASSERT(NULL != task);

    dlnode_t *head = this_ptr(&ready_head);
    ASSERT(!dl_contains(head, &task->ready_node));
}


static NORETURN void idle_proc() {
    while (1) {
        cpu_pause();
        cpu_halt();
    }
}

// 准备调度子系统
// 初始化就绪队列，创建 idle 任务
INIT_TEXT void sched_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        dlnode_t *head = pcpu_ptr(i, &ready_head);
        dl_init_circular(head);

        task_t *idle = pcpu_ptr(i, &idle_task);
        task_create(idle, "idle", idle_proc, NULL);
        task_resume(idle);
    }
}
