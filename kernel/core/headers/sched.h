#ifndef SCHED_H
#define SCHED_H

#include <task.h>

#define PRIORITY_NUM 32 // 最多 32 个优先级

// 中断响应 bottom-half、log 输出等核心系统任务具有最高优先级
// 需要长时间运行，不需要快速响应的，如 housekeeping，具有较低优先级

extern PCPU_BSS task_t *g_tid_prev;
extern PCPU_BSS task_t *g_tid_next;

uint16_t sched_stop(task_t *task, uint16_t bits);
uint16_t sched_cont(task_t *task, uint16_t bits);

void sched_tick();

INIT_TEXT void sched_init();

#endif // SCHED_H
