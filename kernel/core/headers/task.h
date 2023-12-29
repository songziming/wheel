#ifndef TASK_H
#define TASK_H

#include <def.h>
#include <arch_types.h>
#include <process.h>


typedef struct task {
    arch_tcb_t  arch;
    const char *name;

    size_t      stack_pa;
    vmrange_t   stack_va;

    process_t  *process;
} task_t;

extern PCPU_BSS task_t *g_tid_prev;
extern PCPU_BSS task_t *g_tid_next;


rc_t task_create(task_t *task, const char *name, void *entry, process_t *proc);


#endif // TASK_H
