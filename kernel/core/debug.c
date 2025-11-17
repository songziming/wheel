#include "debug.h"
#include <arch_api.h>
#include <kstring.h>
#include <format.h>

log_func_t g_log_func = NULL;

static void log_cb(void *user UNUSED, const char *s, size_t n) {
    if (g_log_func) {
        g_log_func(s, n);
    }
}

void logk(const char *fmt, ...) {
    char tmp[1024];

    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), log_cb, NULL, fmt, args);
    va_end(args);

    // size_t n = kstrlen(fmt);
    // if (g_log_func) {
    //     g_log_func(fmt, n);
    // }
}

void panic(const char *fmt, ...) {}



//------------------------------------------------------------------------------
// 错误处理
//------------------------------------------------------------------------------

static void log_stacktrace() {
    size_t frames[32];
    int depth = arch_unwind(frames, 32);
    for (int i = 0; i < depth; ++i) {
        logk("  - %02d. 0x%zx\n", i, frames[i]);
    }
}

// 断言失败
void assertion_fail(const char *file, int line, const char *func) {
    logk("Assertion fail: %s:%d func:%s\n", file, line, func);
    log_stacktrace();
}


//------------------------------------------------------------------------------
// 栈溢出保护，编译选项 -fstack-protector
//------------------------------------------------------------------------------

#ifndef UNIT_TEST

const uintptr_t __stack_chk_guard = 0x595e9fbd94fda766ULL;

void __stack_chk_fail() {
    logk("fatal: stack smashing detected\n");
    log_stacktrace();
}

#endif // UNIT_TEST
