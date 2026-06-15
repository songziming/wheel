#ifndef PROCESS_H
#define PROCESS_H

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
} process_t;


INIT_TEXT void process_init();
process_t *process_create();
void process_destroy(process_t *pid);

void task_enter_process(process_t *pid);
void task_leave_process(process_t *pid);


#endif // PROCESS_H
