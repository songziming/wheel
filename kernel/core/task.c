#include "task.h"
#include <arch_api.h>
#include <dllist.h>

// 任务管理 & 调度 & 定时任务

static int g_tick = 0;


// 代表一个定时任务
typedef struct timer {
    dlnode_t    dl;
    int         delta;  // 和前一个 timer 相差多少个 tick
    void (*func)(void *arg1, void *arg2);
    void       *arg1;
    void       *arg2;
} timer_t;

// 定时任务队列
static dlnode_t timer_q;



void tick_advance() {
    if (0 == cpu_index()) {
        ++g_tick;
    }
}
