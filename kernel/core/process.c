#include "process.h"
#include <pool_slub.h>

static pool_t g_pcb_pool;

static spin_t g_pcb_list_lock = SPIN_INIT;
static dlnode_t g_pcb_head; // 管理所有 PCB

INIT_TEXT void process_init() {
    pool_init(&g_pcb_pool, sizeof(process_t));
    dl_init_circular(&g_pcb_head);
}

process_t *process_create() {
    process_t *proc = pool_alloc(&g_pcb_pool);
    proc->lock = SPIN_INIT;
    dl_init_circular(&proc->tasks_head);
    vmspace_init(&proc->vm, 0x100000, 1UL << 32);

    int key = irq_spin_take(&g_pcb_list_lock);
    dl_insert_before(&proc->objnode, &g_pcb_head);
    irq_spin_give(&g_pcb_list_lock, key);

    return proc;
}

void process_destroy(process_t *proc) {
    int key = irq_spin_take(&g_pcb_list_lock);
    dl_remove(&proc->objnode);
    irq_spin_give(&g_pcb_list_lock, key);
    pool_free(&g_pcb_pool, proc);
}
