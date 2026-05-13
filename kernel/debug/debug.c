#include "debug.h"
#include <arch_api.h>
#include <kstring.h>
#include <format.h>

log_func_t g_log_func = NULL;

static void log_cb(void *user UNUSED, const char **s, size_t *len) {
    if (g_log_func) {
        g_log_func(*s, *len);
    }
}

void logk(const char *fmt, ...) {
    char tmp[1024];

    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), log_cb, NULL, fmt, args);
    va_end(args);
}


void log_stacktrace() {
    size_t frames[32];
    int depth = arch_unwind(frames, 32);
    for (int i = 0; i < depth; ++i) {
        logk("  - %02d. 0x%zx\n", i, frames[i]);
    }
}

NORETURN void panic(const char *fmt, ...) {
    char tmp[1024];

    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), log_cb, NULL, fmt, args);
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
void assertion_fail(const char *file, int line, const char *func) {
    logk("Assertion fail: %s:%d func:%s\n", file, line, func);
    log_stacktrace();
}

#endif
