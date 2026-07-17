#include "proc.h"
#include <kobj.h>
#include <task.h>
#include <debug.h>



// 不是进程拥有任务，而是任务共享进程（类似于 Linux mm_struct）
// PCB 是一种共享资源，多个线程可以使用同一个 PCB
static kclass_t g_pcb_class;
// static kclass_t g_rng_class;


// 属于这个进程的所有vmrange都是动态分配的，需要遍历将其删除
// 只剩最后一个引用，可以安全访问，无需加锁
static void proc_cleanup(void *ptr) {
    proc_t *pid = (proc_t *)ptr;
    logk("destroying process %s-%d\n", kobj_name(ptr), pid->id);

    dlnode_t *dl = pid->vm.head.next;
    while (dl != &pid->vm.head) {
        vmrange_t *rng = containerof(dl, vmrange_t, dl);
        dl = dl->next;
        vmspace_remove(&pid->vm, rng);
    }

    // 删除地址空间
    // 注意，当前可能正在使用此页表，最好先切到内核页表，或者在 ISR 里执行此函数
    mmu_delete(pid->vm.table);
}


INIT_TEXT void process_init() {
    kclass_register(&g_pcb_class, "PCB", sizeof(proc_t), proc_cleanup);
    // kclass_register(&g_rng_class, "VM", sizeof(vmrange_t), NULL);
}

static _Atomic int g_next_id = 0;

proc_t *proc_make(const char *name) {
    proc_t *pid = kobj_make(&g_pcb_class, name);
    if (NULL == pid) {
        return NULL;
    }

    pid->lock = SPINLOCK_INIT;

    // dl_init_circular(&pid->tasks_head);
    // pid->task_num = 0;

    pid->id = atomic_fetch_add(&g_next_id, 1);

    vmspace_init(&pid->vm, 0x100000, 1UL << 32);
    pid->vm.table = mmu_create();
    mmu_copykernel(pid->vm.table, g_kernel_vm.table);

    return pid;
}

void proc_drop(proc_t *pid) {
    kobj_drop(&g_pcb_class, pid);
}

// 将当前任务迁移到进程，切换到新的地址空间
void task_enter_process(proc_t *pid) {
    task_t *tid = current_task();

    kobj_keep(pid);

    proc_t *old = tid->process;
    tid->process = pid;
    tid->pgtbl = pid->vm.table;
    mmu_usetable(tid->pgtbl);

    if (old) {
        kobj_drop(&g_pcb_class, old);
    }
}

// 将当前任务移出进程，回到内核地址空间
void task_leave_process() {
    task_t *tid = current_task();

    // TODO 用户栈需要删除

    // 重新切换到内核页表
    tid->pgtbl = g_kernel_vm.table;
    mmu_usetable(tid->pgtbl);

    // 检查进程是否引用计数归零，回收PCB
    kobj_drop(&g_pcb_class, tid->process);
    tid->process = NULL;
}


// 对进程地址空间的操作，必须由这个进程的任务执行
// 也就是目标地址空间必须是当前地址空间


// 为这个进程的地址空间分配一段虚拟地址
// void process_valloc(proc_t *pid, size_t size, const char *name) {
//     vmrange_t *rng = kobj_make(&g_rng_class, "vmrng");
//     if (NULL == rng) {
//         panic("cannot allocate section\n");
//     }
//     {
//         SPINLOCK_SCOPED(&pid->lock);
//         vmspace_alloc(&pid->vm, rng, size, PT_KERNEL, MMU_WRITE);
//     }
//     rng->desc = "process dynamic";
// }
