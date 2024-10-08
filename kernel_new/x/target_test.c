#include "target_test.h"

#include <library/debug.h>
#include <proc/tick.h>


// 一些在 OS 中执行的测试用例


//------------------------------------------------------------------------------
// 定时器周期性触发
//------------------------------------------------------------------------------

static timer_t wd;

static void wd_func(void *a1 UNUSED, void *a2 UNUSED) {
    static int cnt = 0;
    log("watchdog-%d\n", cnt++);

    timer_start(&wd, 20, (timer_func_t)wd_func, 0, 0);
}

void test_timer_periodic() {
    timer_start(&wd, 20, (timer_func_t)wd_func, 0, 0);
}
