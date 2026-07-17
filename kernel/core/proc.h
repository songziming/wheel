#ifndef PROC_H
#define PROC_H

#include <spinlock.h>
#include <dllist.h>
#include <vmspace.h>

typedef struct proc {
    spinlock_t  lock;

    vmspace_t   vm;
    vmrange_t   code;
    vmrange_t   data;

    int         id;
    size_t      entry;

    // 用户栈，可以不只一个，响应 rpc 时从资源池中挑选一个
    vmrange_t   ustack;
} proc_t;


INIT_TEXT void process_init();
proc_t *proc_make(const char *name);
void proc_drop(proc_t *pid);
void task_enter_process(proc_t *pid);
void task_leave_process();


#endif // PROC_H
