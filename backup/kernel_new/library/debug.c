#include "debug.h"
#include "format.h"
#include "string.h"
#include "symbols.h"
#include "dwarf.h"
#include <arch_intf.h>


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
// 输出调用栈
//------------------------------------------------------------------------------

void print_frames(const size_t *frames, int num) {
    for (int i = 0; i < num; ++i) {
        size_t rela;
        const char *name = sym_resolve(frames[i], &rela);

        const char *file;
        int line = addr_to_line(frames[i] - 1, &file);

        if (file && line) {
            log(" -> #%d  %s at %s:%d\n", i, name, file, line);
        } else {
            log(" -> #%d  %s + 0x%zx\n", i, name, rela);
        }
    }
}

void log_stacktrace() {
    size_t frames[32];
    int depth = arch_unwind(frames, 32);
    print_frames(&frames[1], depth - 1);
}


//------------------------------------------------------------------------------
// 处理断言失败
//------------------------------------------------------------------------------

void assertion_fail(const char *file, int line, const char *func) {
    log("Assertion fail: %s:%d func:%s\n", file, line, func);
    log_stacktrace();
}


//------------------------------------------------------------------------------
// 栈溢出保护，编译选项 -fstack-protector
//------------------------------------------------------------------------------

// linux kernel 把 stack protector 放在了 panic.c 里面

#ifndef UNIT_TEST

const uintptr_t __stack_chk_guard = 0x595e9fbd94fda766ULL;

void __stack_chk_fail() {
    log("fatal: stack smashing detected\n");
    // klog_stacktrace();
    // emu_exit(1);
}

#endif // UNIT_TEST
