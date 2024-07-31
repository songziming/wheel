#include "debug.h"
#include "format.h"
#include "string.h"


//------------------------------------------------------------------------------
// 打印调试输出
//------------------------------------------------------------------------------

static void (*g_log_func)(const char *, size_t) = NULL;

void set_log_func(void (*func)(const char *, size_t)) {
    g_log_func = func;
}

void log(const char *fmt, ...) {
    char tmp[1024];

    if (NULL == g_log_func) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), g_log_func, fmt, args);
    va_end(args);
}


//------------------------------------------------------------------------------
// 处理断言失败
//------------------------------------------------------------------------------

void assertion_fail(const char *file, int line, const char *func) {
    log("Assertion fail: %s:%d func:%s\n", file, line, func);
}


//------------------------------------------------------------------------------
// 栈溢出保护，编译选项 -fstack-protector
//------------------------------------------------------------------------------

// linux kernel 把 stack protector 放在了 panic.c 里面

const uintptr_t __stack_chk_guard = 0x595e9fbd94fda766ULL;

void __stack_chk_fail() {
    log("fatal: stack smashing detected\n");
    // klog_stacktrace();
    // emu_exit(1);
}
