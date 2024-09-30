#include "work.h"
#include <arch_intf.h>


static PERCPU_BSS dlnode_t g_work_q;



INIT_TEXT void work_init() {
    for (int i = 0; i < cpu_count(); ++i) {
        dlnode_t *head = percpu_ptr(i, &g_work_q);
        dl_init_circular(head);
    }
}

void work_defer(work_t *work, work_func_t func, void *arg1, void *arg2) {
    work->func = func;
    work->arg1 = arg1;
    work->arg2 = arg2;

    int key = cpu_int_lock();
    dlnode_t *head = thiscpu_ptr(&g_work_q);
    dl_insert_before(&work->dl, head);
    cpu_int_unlock(key);
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
