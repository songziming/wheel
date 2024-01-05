#ifndef SCHED_H
#define SCHED_H

#include <task.h>

#define PRIORITY_NUM 32

extern PCPU_BSS task_t *g_tid_prev;
extern PCPU_BSS task_t *g_tid_next;

INIT_TEXT void sched_init();
void sched_resume(task_t *task);

#endif // SCHED_H
