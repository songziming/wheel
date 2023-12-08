#ifndef TICK_H
#define TICK_H

#include <base.h>
#include <arch_interface.h>
#include <page.h>
#include <vmspace.h>
#include <dllist.h>


// 任务控制块（TCB）
typedef struct task {
    task_regs_t regs;

    vmrange_t   stack_range;    // 虚拟地址范围
    page_list_t stack_pages;    // 物理页链表

    int         tick_remain;
    int         tick_total;
    dlnode_t    dl;
} task_t;

extern PCPU_DATA task_t *g_tid_curr;
extern PCPU_DATA task_t *g_tid_next;

void tick_advance();

#endif // TICK_H
