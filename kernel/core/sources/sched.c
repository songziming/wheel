// 任务调度

#include <sched.h>
#include <wheel.h>


PCPU_BSS task_t *g_tid_prev;  // 当前正在运行的任务
PCPU_BSS task_t *g_tid_next;  // 下次中断将要切换的任务

