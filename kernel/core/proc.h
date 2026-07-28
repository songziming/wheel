#ifndef PROC_H
#define PROC_H

#include <spinlock.h>
#include <dllist.h>
#include <vmspace.h>

typedef struct proc {
    spinlock_t  lock;

    vmspace_t   vm;
    vmrange_t  *ustack;

    int         id;
    size_t      entry;

} proc_t;


INIT_TEXT void process_init();
proc_t *proc_make(const char *name);
void proc_drop(proc_t *pid);

vmrange_t *proc_valloc(proc_t *pid, size_t va, size_t size, mmu_attr_t attrs);

void task_enter_process(proc_t *pid);
void task_leave_process();


#endif // PROC_H
