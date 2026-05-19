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

    // 恢复任务的过程中，保持中断关闭
    uint64_t cpu_mask = 0UL;
    while (dl != &fence->penders) {
        fence_pender_t *pender = containerof(dl, fence_pender_t, dl);
        dl = dl->next;
        cpu_mask |= task_start(pender->tid);
    }
    notify_resched(cpu_mask);

    // 打开中断，当前 cpu 也要检查抢占
    irq_spin_give(&fence->lock, key);
    arch_task_switch();
}
