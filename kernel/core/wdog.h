#ifndef WDOG_H
#define WDOG_H

#include <dllist.h>

// TODO 可以改名为 wdog，借鉴 vxworks

// 代表一个定时任务
typedef struct wdog wdog_t;
typedef void (*wdog_cb_t)(wdog_t*);
struct wdog {
    dlnode_t    dl;
    int         delta;  // 和前一个 job 相差多少个 tick
    wdog_cb_t   func;
};

INIT_TEXT void wdog_init();
void wdog_process();
void wdog_start(wdog_t *wd, wdog_cb_t cb, int tick);
void wdog_cancel(wdog_t *wd);

#endif // WDOG_H
