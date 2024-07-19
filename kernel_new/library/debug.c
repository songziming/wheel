#include "debug.h"
#include "format.h"
#include "string.h"



// TODO 做成函数指针，由 arch 设置 log_handler
void serial_puts(const char *s, size_t n);

void log(const char *fmt, ...) {
    char tmp[1024];

    va_list args;
    va_start(args, fmt);
    format(tmp, sizeof(tmp), serial_puts, fmt, args);
    va_end(args);
}




// linux kernel 把 stack protector 放在了 panic.c 里面
const uintptr_t __stack_chk_guard = 0x595e9fbd94fda766ULL;
void __stack_chk_fail() {
    log("fatal: stack smashing detected\n");
    // klog_stacktrace();
    // emu_exit(1);
}
