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
    vmspace_t   vm;
    size_t      pgtbl;  // 页表
} process_t;

// TODO vmspace 去掉锁，去掉页表操作，vmrange 去掉物理页和名称
//      vmspace 模块只维护虚拟地址空间，页表的操作交给 process
//      pool 也去掉锁，由使用者防止竞争

// 代表地址空间中的一段
typedef struct section {
    vmrange_t   rng;
    const char *name;
    pglist_t    pages;
    mmu_attr_t  attrs;
} section_t;


INIT_TEXT void process_init();
process_t *process_create();
void process_destroy(process_t *proc);


#endif // PROCESS_H
