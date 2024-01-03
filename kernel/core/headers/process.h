#ifndef PROCESS_H
#define PROCESS_H

#include <vmspace.h>

typedef struct process {
    vmspace_t space; // 用户态虚拟地址空间布局
    size_t    table; // 用户态地址映射
    dlnode_t  proc_head;
} process_t;

process_t *get_kernel_process();

void process_create(process_t *proc);


#endif // PROCESS_H
