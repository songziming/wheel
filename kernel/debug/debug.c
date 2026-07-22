#include "debug.h"
#include <arch_api.h>
#include <kstring.h>
#include <format.h>

#include <spinlock.h>
#include <fifo.h>

log_func_t g_log_func = NULL;

static spinlock_t log_lock = SPINLOCK_INIT;
static fifo_t log_fifo;

static char log_buff[PAGE_SIZE];

INIT_TEXT void log_init() {
    fifo_init(&log_fifo, log_buff, sizeof(log_buff));
}

// 很多地方都要调用这个函数，包括中断里面
void logk(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    {
        SPINLOCK_SCOPED(&log_lock);
        fifo_vprint(&log_fifo, fmt, args, g_log_func);
    }
    va_end(args);
}

void log_stacktrace() {
    size_t frames[32];
    int depth = arch_unwind(frames, 32);
    for (int i = 0; i < depth; ++i) {
        logk("  - %02d. 0x%zx\n", i, frames[i]);
    }
}

// 发生严重错误，应该使用 console 输出
NORETURN void panic(const char *fmt, ...) {
    // 广播 IPI，停止其他 CPU
    arch_send_ipi(IPI_ALL_EXCLUDING_SELF, VEC_IPI_STOPALL);

    va_list args;
    va_start(args, fmt);
    {
        SPINLOCK_SCOPED(&log_lock);
        fifo_vprint(&log_fifo, fmt, args, g_log_func);
    }
    va_end(args);

    log_stacktrace();

    while (1) {
        cpu_pause();
        cpu_halt();
    }
}



//------------------------------------------------------------------------------
// 错误处理
//------------------------------------------------------------------------------

#ifndef UNIT_TEST

// 断言失败
// 保持运行，不要停机
void assertion_fail(const char *file, int line, const char *func) {
    logk("Assertion fail: %s:%d func:%s\n", file, line, func);
    log_stacktrace();
}

#endif
