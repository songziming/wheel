#ifndef PROCESS_H
#define PROCESS_H

#include <spinlock.h>
#include <dllist.h>
#include <vmspace.h>

typedef struct process {
    spinlock_t      lock;
    dlnode_t    objnode;    // item in process-table
    const char *name;
    dlnode_t    tasks_head; // list of tasks
    vmspace_t   vm;
    size_t      pgtbl;  // 页表
} process_t;

// TODO vmspace 去掉锁，去掉页表操作，只维护虚拟地址空间
//      页表的操作交给 process

#endif // PROCESS_H
