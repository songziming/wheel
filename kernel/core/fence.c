#include "fence.h"
#include <task.h>


typedef struct fence_pender {
    dlnode_t dl;
    task_t  *tid;
} fence_pender_t;


void fence_init(fence_t *fence) {
    fence->lock = SPIN_INIT;
    dl_init_circular(&fence->penders);
}

void fence_wait(fence_t *fence) {
    fence_pender_t pender;
    pender.tid = THISCPU_GET(g_tid_prev);

    int key = irq_spin_take(&fence->lock);
    dl_insert_before(&pender.dl, &fence->penders);
    irq_spin_give(&fence->lock, key);
}

void fence_signal(fence_t *fence) {
    int key = irq_spin_take(&fence->lock);

    dlnode_t *dl = fence->penders.next;
    dl_init_circular(&fence->penders);

    uint64_t cpu_mask = 0UL;
    while (dl != &fence->penders) {
        fence_pender_t *pender = containerof(dl, fence_pender_t, dl);
        dl = dl->next;
        task_start(pender->tid);
    }

    while (cpu_mask) {
        int cpu = __builtin_ctzll(cpu_mask);
        cpu_mask &= cpu_mask - 1;
        arch_send_ipi(cpu, VEC_IPI_RESCHED);
    }

    irq_spin_give(&fence->lock, key);
}
