#include "process.h"
#include <pool_slub.h>
#include <debug.h>


static spinlock_t g_pcb_lock = SPINLOCK_INIT;
static pool_t     g_pcb_pool;
static dlnode_t   g_pcb_head; // 管理所有 PCB


static spinlock_t g_sec_lock = SPINLOCK_INIT;
static pool_t     g_sec_pool;


// 如果是内核进程，则需要传入页表，而不是自己创建
static void process_setup(process_t *pid, size_t pgtbl) {
    pid->lock = SPINLOCK_INIT;
    dl_init_circular(&pid->tasks_head);
    vmspace_init(&pid->vm, 0x100000, 1UL << 32);
    if (0 == pgtbl) {
        pgtbl = mmu_create();
    }
    pid->pgtbl = pgtbl;
}



INIT_TEXT void process_init() {
    pool_init(&g_pcb_pool, sizeof(process_t));
    dl_init_circular(&g_pcb_head);
    pool_init(&g_sec_pool, sizeof(section_t));
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

    process_setup(pid, 0);
    return pid;
}

void process_destroy(process_t *pid) {
    SPINLOCK_SCOPED(&g_pcb_lock);
    dl_remove(&pid->objnode);
    pool_free_nolock(&g_pcb_pool, pid);
}


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
