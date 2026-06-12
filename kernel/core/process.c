#include "process.h"
#include <pool_slub.h>
#include <debug.h>


static spinlock_t g_pcb_lock = SPINLOCK_INIT;
static pool_t     g_pcb_pool;
static dlnode_t   g_pcb_head; // 管理所有 PCB


static spinlock_t g_sec_lock = SPINLOCK_INIT;
static pool_t     g_sec_pool;


process_t g_kernel_proc;


// 如果是内核进程，则需要传入页表，而不是自己创建
static void process_setup(process_t *pid) {
    pid->lock = SPINLOCK_INIT;
    dl_init_circular(&pid->tasks_head);
    vmspace_init(&pid->vm, 0x100000, 1UL << 32);
    pid->pgtbl = mmu_create();
}



INIT_TEXT void process_init() {
    pool_init(&g_pcb_pool, sizeof(process_t));
    dl_init_circular(&g_pcb_head);
    pool_init(&g_sec_pool, sizeof(section_t));

    // 内核进程不能删除
    process_setup(&g_kernel_proc);
    g_kernel_proc.name = "kernel";
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

    process_setup(pid);
    return pid;
}

void process_destroy(process_t *pid) {
    SPINLOCK_SCOPED(&g_pcb_lock);
    dl_remove(&pid->objnode);
    pool_free_nolock(&g_pcb_pool, pid);
}


// 注册一个存在的 section，但这个 section 不是自己创建的
// 用于 kernel 添加现有布局
// 用于进程映射一段共享内存
// 同时编辑页表，创建映射
void process_map_section(process_t *pid, section_t *sec) {
    SPINLOCK_SCOPED(&pid->lock);
    vmspace_insert(&pid->vm, &sec->rng);

    size_t va = sec->rng.vaddr;
    for (uint32_t blk = sec->pages.head; blk; blk = g_pages[blk].next) {
        size_t size = PAGE_SIZE << g_pages[blk].rank;
        mmu_map(pid->pgtbl, va, va + size, (size_t)blk << PAGE_SHIFT, MMU_WRITE);
        va += size;
    }
}


void process_unmap(process_t *pid, section_t *sec) {
    {
        SPINLOCK_SCOPED(&pid->lock);
        vmspace_remove(&pid->vm, &sec->rng);
        mmu_unmap(pid->pgtbl, sec->rng.vaddr, sec->rng.vend);
    }

    // 只有当前地址空间才需要 shootdown
    // 如果进程即将退出，则无需 shootdown（检查进程状态）
    tlb_shootdown(sec->rng.vaddr, sec->rng.vend);
}


// 对进程地址空间的操作，必须由这个进程的任务执行
// 也就是目标地址空间必须是当前地址空间


// 为这个进程的地址空间分配一段虚拟地址
void process_valloc(process_t *pid, size_t size) {
    section_t *sec;
    {
        SPINLOCK_SCOPED(&g_sec_lock);
        sec = pool_alloc_nolock(&g_sec_pool);
    }
    if (NULL == sec) {
        panic("cannot allocate section\n");
    }

    {
        SPINLOCK_SCOPED(&pid->lock);
        vmspace_alloc(&pid->vm, &sec->rng, size, PT_KERNEL, MMU_WRITE);
    }
}
