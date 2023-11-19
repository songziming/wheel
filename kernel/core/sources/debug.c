#include <wheel.h>
#include <format.h>


// 打印调试输出
// 只需要定义 log 函数接口、assert_fail 函数、ASSERT宏，具体实现方式交给 arch
// 堆栈展开、符号解析这些功能应该交给 arch，但解析 elf 应该放在 lib

static log_func_t g_log_func = NULL;

void set_log_func(log_func_t func) {
    g_log_func = func;
}

static void print_cb(void *para, const char *s, size_t n) {
    (void)para;

    // TODO 启动阶段，准备一段 buffer 作为临时文件缓冲区（ringbuf）
    //      临时 buffer 满了就覆盖最早写入的内容
    // TODO 文件系统启动之后，将调试输出写入文件磁盘
    // TODO 提供虚拟文件 /var/dbg，包括磁盘上和 ringbuf 中的内容

    if (NULL != g_log_func) {
        g_log_func(s, n);
    }
}

// TODO 增加多种 LOG 级别
void klog(const char *fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), print_cb, NULL, fmt, args);
    va_end(args);
}



void handle_assert_fail(const char *file, const char *func, int line) {
    klog("Assert failed %s:%s:%d\n", file, func, line);
    // TODO 打印调用栈，并跳过这个函数
    vmshutdown(1);
}
