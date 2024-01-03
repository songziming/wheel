#ifndef TASK_H
#define TASK_H

#include <def.h>
#include <arch_types.h>
#include <process.h>


typedef enum task_state {
    TASK_READY,
    TASK_PENDING,   // 因为 semaphore、pipe、msgqueue 等资源而阻塞，处于它们的等待队列中
    TASK_STOPPED,   // 被手动暂停，不在就绪队列中
    TASK_DELETED,   // 已停止，即将删除（已预约下一次 work_queue）
} task_state_t;

typedef struct task {
    arch_tcb_t   arch;

    task_state_t state;
    const char  *name;

    size_t       stack_pa;
    vmrange_t    stack_va;

    process_t   *process;
    dlnode_t     proc_node; // 任务在进程中的节点

    dlnode_t     ready_node; // 任务在就绪队列中的节点
} task_t;


extern PCPU_BSS task_t *g_tid_prev;
extern PCPU_BSS task_t *g_tid_next;


rc_t task_create(task_t *task, const char *name, void *entry, process_t *proc);


#endif // TASK_H
