#ifndef SCHED_H
#define SCHED_H

#include <library/dllist.h>

// 代表一个线程
// 或许可以改成 sched_item_t
typedef struct task {
    void    *arch_regs; // 指向中断栈上保存的上下文
    int         priority;   // 最大取值 31，可以换成 bitfield
    dlnode_t    q_node; // 就绪队列/阻塞队列节点
    const char *name;
} task_t;

extern PCPU_BSS task_t *g_tid_prev;
extern PCPU_BSS task_t *g_tid_next;

#endif // SCHED_H