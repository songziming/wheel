#include "process.h"
#include <pool_slub.h>
#include <task.h>
#include <debug.h>



// 不是进程拥有任务，而是任务共享进程（类似于 Linux mm_struct）
// PCB 是一种共享资源，多个线程可以使用同一个 PCB


static spinlock_t g_pcb_lock = SPINLOCK_INIT;
static pool_t     g_pcb_pool;
// static dlnode_t   g_pcb_head; // 管理所有 PCB
static DEFINE_DL_HEAD(g_pcb_head);


// 用户态的地址空间需要动态分配 vmrange
static spinlock_t g_rng_lock = SPINLOCK_INIT;
static pool_t     g_rng_pool;



INIT_TEXT void process_init() {
    pool_init(&g_pcb_pool, sizeof(process_t));
    // dl_init_circular(&g_pcb_head);
    pool_init(&g_rng_pool, sizeof(vmrange_t));
}

process_t *process_create() {
    process_t *pid;
    {
        SPINLOCK_SCOPED(&g_pcb_lock);
        pid = pool_alloc_nolock(&g_pcb_pool);
        if (NULL == pid) {
            return NULL;
        }
        dl_insert_before(&pid->objnode, &g_pcb_head);
    }

    pid->lock = SPINLOCK_INIT;

    dl_init_circular(&pid->tasks_head);
    pid->task_num = 0;

    vmspace_init(&pid->vm, 0x100000, 1UL << 32);
    pid->vm.table = mmu_create();
    mmu_copykernel(pid->vm.table, g_kernel_vm.table);

    return pid;
}

void process_destroy(process_t *pid) {
    // 属于这个进程的所有vmrange都是动态分配的，需要遍历将其删除
    dlnode_t *dl = pid->vm.head.next;
    while (dl != &pid->vm.head) {
        vmrange_t *rng = containerof(dl, vmrange_t, dl);
        dl = dl->next;
        {
            SPINLOCK_SCOPED(&g_rng_lock);
            pool_free_nolock(&g_rng_pool, rng);
        }
    }

    // 删除地址空间
    // 注意，当前可能正在使用此页表，最好先切到内核页表，或者在 ISR 里执行此函数
    mmu_delete(pid->vm.table);

    // 将PCB也删除
    {
        SPINLOCK_SCOPED(&g_pcb_lock);
        dl_remove(&pid->objnode);
        pool_free_nolock(&g_pcb_pool, pid);
    }
}


// 将当前任务迁移到进程
void task_enter_process(process_t *pid) {
    task_t *tid = current_task();
    ASSERT(NULL == tid->process);

    task_take_from_kernel(tid);
    {
        SPINLOCK_SCOPED(&pid->lock);
        // dl_insert_before(&tid->objnode, &pid->tasks_head);
        ++pid->task_num;
    }

    tid->process = pid;
    tid->pgtbl = pid->vm.table;
    mmu_usetable(tid->pgtbl);
}

// 将当前任务移出进程，回到内核任务
// 移出之后任务还可以继续运行
void task_leave_process(process_t *pid) {
    task_t *tid = current_task();
    ASSERT(pid == tid->process);

    SPINLOCK_SCOPED(&pid->lock);
    --pid->task_num;
    // dl_remove(&tid->objnode);

    // TODO 用户栈需要删除

    // 重新切换到内核页表
    tid->process = NULL;
    tid->pgtbl = g_kernel_vm.table;
    mmu_usetable(tid->pgtbl);

    // TODO 检查进程是否引用计数归零，回收PCB
}


// 对进程地址空间的操作，必须由这个进程的任务执行
// 也就是目标地址空间必须是当前地址空间


// 为这个进程的地址空间分配一段虚拟地址
void process_valloc(process_t *pid, size_t size) {
    vmrange_t *rng;
    {
        SPINLOCK_SCOPED(&g_rng_lock);
        rng = pool_alloc_nolock(&g_rng_pool);
    }
    if (NULL == rng) {
        panic("cannot allocate section\n");
    }
    {
        SPINLOCK_SCOPED(&pid->lock);
        vmspace_alloc(&pid->vm, rng, size, PT_KERNEL, MMU_WRITE);
    }
    rng->desc = "process dynamic";
}
