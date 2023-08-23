#include <debug.h>
#include <libk_format.h>

dbg_print_func_t g_dbg_print_func;

static void print_cb(void *para, const char *s, size_t n) {
    (void)para;

    // TODO 将调试输出写入文件 /var/dbg

    if (NULL != g_dbg_print_func) {
        g_dbg_print_func(s, n);
    }
}

// 打印调试输出
void dbg_print(const char *fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), print_cb, NULL, fmt, args);
    va_end(args);
}

void report_assert_fail(const char *file, const char *func, int line) {
    dbg_print("Assert failed %s:%s:%d\n", file, func, line);
}
