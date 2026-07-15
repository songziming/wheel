#ifndef PROC_H
#define PROC_H

#include <spinlock.h>
#include <dllist.h>
#include <vmspace.h>

typedef struct process {
    spinlock_t  lock;
    dlnode_t    objnode;    // item in process-table
    const char *name;
    dlnode_t    tasks_head; // list of tasks
    int         task_num;
    vmspace_t   vm;
} proc_t;


INIT_TEXT void process_init();
proc_t *proc_make();

void task_enter_process(proc_t *pid);
void task_leave_process(proc_t *pid);


#endif // PROC_H
