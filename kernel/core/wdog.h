#ifndef WDOG_H
#define WDOG_H

#include <dllist.h>

// 代表一个定时任务
typedef struct wdog wdog_t;
typedef void (*wdog_cb_t)(wdog_t*);
struct wdog {
    dlnode_t    dl;
    _Atomic int state;
    int         delta;  // 和前一个 job 相差多少个 tick
    wdog_cb_t   func;
};

// wdog 状态机（wdog_t.state 的取值）
// WDOG_IDLE  未放入定时器队列、可用（创建/取消/回调结束后回到此态）
// WDOG_ARMED 已放入定时器队列，等待超时
// WDOG_FIRED 已超时，callback 正在执行
// 外部代码只需用 WDOG_IDLE 初始化；WDOG_ARMED/WDOG_FIRED 是 wdog.c 内部协议
enum wdog_state {
    WDOG_IDLE  = 0,
    WDOG_ARMED = 1,
    WDOG_FIRED = 2,
};

// INIT_TEXT void wdog_init();
void wdog_process();
void wdog_start(wdog_t *wd, wdog_cb_t cb, int tick);
void wdog_cancel(wdog_t *wd);

#endif // WDOG_H
