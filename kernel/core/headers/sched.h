#ifndef SCHED_H
#define SCHED_H

#include <def.h>
#include <arch_types.h>

typedef struct task {
    arch_tcb_t  arch;
    const char *name;
    size_t      entry;
} task_t;

extern PCPU_BSS task_t *g_tid_prev;
extern PCPU_BSS task_t *g_tid_next;

#endif // SCHED_H
