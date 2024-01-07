#ifndef SCHED_H
#define SCHED_H

#include <task.h>

#define PRIORITY_NUM 32

extern PCPU_BSS task_t *g_tid_prev;
extern PCPU_BSS task_t *g_tid_next;

uint16_t sched_stop(task_t *task, uint16_t bits);
uint16_t sched_cont(task_t *task, uint16_t bits);

INIT_TEXT void sched_init();

#endif // SCHED_H
