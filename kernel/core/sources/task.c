// 任务调度

#include <task.h>
#include <wheel.h>
#include <page.h>
#include <vmspace.h>
#include <str.h>



PCPU_BSS task_t *g_tid_prev;  // 当前正在运行的任务
PCPU_BSS task_t *g_tid_next;  // 下次中断将要切换的任务



// void task_create_ex(task_t *task, const char *name, void *entry, process_t *proc,
//         int stack_rank, size_t args[4]) {
//     //
// }

// 任务创建失败则返回非零
rc_t task_create(task_t *task, const char *name, void *entry, process_t *proc) {
    ASSERT(NULL != task);
    ASSERT(NULL != entry);

    if (NULL == proc) {
        proc = &g_kernel_proc;
    }

    size_t stack_size = PAGE_SIZE << TASK_STACK_RANK;
    size_t va = vmspace_search(&proc->space, STACK_AREA_ADDR, STACK_AREA_END,
        stack_size);
    if (INVALID_ADDR == va) {
        return RC_NO_FREE_RANGE;
    }

    size_t pa = pages_alloc(TASK_STACK_RANK, PT_KERNEL_STACK);
    if (INVALID_ADDR == pa) {
        return RC_NO_FREE_PAGE;
    }

    vmrange_init(&task->stack_va, va, va + stack_size, name);
    vmspace_insert(&proc->space, &task->stack_va);
    mmu_map(proc->table, va, va + stack_size, pa, MMU_WRITE);
    bset((char *)va, 0, stack_size);

    arch_tcb_init(&task->arch, entry, va + stack_size);
    task->stack_pa = pa;
    task->name = name;
    task->process = proc;
    return RC_OK;
}
