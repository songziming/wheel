#include "work.h"
#include <arch_intf.h>
#include <library/spin.h>


static PERCPU_BSS spin_t g_work_spin;
static PERCPU_BSS dlnode_t g_work_q;



INIT_TEXT void work_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        spin_t *lock = percpu_ptr(i, &g_work_spin);
        spin_init(lock);
        dlnode_t *head = percpu_ptr(i, &g_work_q);
        dl_init_circular(head);
    }
}

void work_defer(work_t *work, work_func_t func, void *arg1, void *arg2) {
    work->func = func;
    work->arg1 = arg1;
    work->arg2 = arg2;

    spin_t *lock = thiscpu_ptr(&g_work_spin);
    int key = irq_spin_take(lock);
    dlnode_t *head = thiscpu_ptr(&g_work_q);
    dl_insert_before(&work->dl, head);
    irq_spin_give(lock, key);
}

// 把异步任务放在另一个 CPU 上运行，然后发送 IPI，触发目标 CPU 的中断
void work_defer_on(work_t *work, int cpu, work_func_t func, void *arg1, void *arg2) {
    work->func = func;
    work->arg1 = arg1;
    work->arg2 = arg2;

    spin_t *lock = percpu_ptr(cpu, &g_work_spin);
    int key = irq_spin_take(lock);
    dlnode_t *head = percpu_ptr(cpu, &g_work_q);
    dl_insert_before(&work->dl, head);
    irq_spin_give(lock, key);

    // arch_emit_ipi(cpu);
}

// 在中断返回阶段执行，
void work_q_flush() {
    dlnode_t *head = thiscpu_ptr(&g_work_q);
    for (dlnode_t *i = head->next; i != head; i = i->next) {
        work_t *work = containerof(i, work_t, dl);
        work->func(work->arg1, work->arg2);
    }
    dl_init_circular(head);
}
