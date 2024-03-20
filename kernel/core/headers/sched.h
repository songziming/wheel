#ifndef SCHED_H
#define SCHED_H

#include <task.h>

#define PRIORITY_NUM 32 // 最多 32 个优先级

typedef struct priority_q {
    uint32_t  priorities; // 位图，表示队列包含的优先级
    dlnode_t *heads[PRIORITY_NUM];
} priority_q_t;

extern PCPU_BSS task_t *g_tid_prev;
extern PCPU_BSS task_t *g_tid_next;


void priority_q_init(priority_q_t *q);
void priority_q_push(priority_q_t *q, task_t *tid, dlnode_t *dl);
dlnode_t *priority_q_head(priority_q_t *q);
void priority_q_remove(priority_q_t *q, task_t *tid, dlnode_t *dl);

task_t *sched_stop_self(uint16_t bits);
int sched_cont(task_t *task, uint16_t bits);

void sched_tick();

INIT_TEXT void sched_init();

#endif // SCHED_H
