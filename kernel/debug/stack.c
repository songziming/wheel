// 栈溢出保护，编译选项 -fstack-protector

#ifndef UNIT_TEST

#include <wheel.h>
#include <debug.h>

const uintptr_t __stack_chk_guard = 0x595e9fbd94fda766ULL;

void __stack_chk_fail() {
    logk("fatal: stack smashing detected\n");
    log_stacktrace();
}

#endif // UNIT_TEST
