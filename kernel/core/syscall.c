#include "syscall.h"
#include <wheel.h>
#include <task.h>
#include <proc.h>

#include <kstring.h>
#include <debug.h>
#include <console.h>

static void do_sys_exit(int64_t ret) {
    logk("[sc] process exit with code %d\n", (int)ret);
    task_exit();
}

static size_t do_sys_read(int fd, char *dst, size_t len) {
    logk("[sc] writing fd=%d, len=%zu, s=%p\n", fd, len, dst);
    if (0 == fd) {
        console_readline(dst, len);
    }
    return 0;
}

static int64_t do_sys_write(int fd, const char *buf, size_t len) {
    logk("[sc] writing fd=%d, len=%zu, s=%p\n", fd, len, buf);
    if ((1 == fd) || (2 == fd)) {
        console_puts(buf, len);
    }
    return 0;
}

static int64_t do_sys_getpid() {
    task_t *self = current_task();
    return (self->process) ? ((proc_t*)self->process)->id : 0;
}

static int64_t do_sys_yield() {
    arch_task_switch();
    return 0;
}

// NULL 表项 fallback
void do_sys_unknown() {
    logk("unknown syscall\n");
}


// 系统调用表，索引 = 调用编号
// NULL 表项由 arch_entries.S 的 syscall_entry fallback 到 do_sys_unknown
CONST void *syscall_tbl[256] = {
    [SYS_exit]   = do_sys_exit,
    [SYS_write]  = do_sys_write,
    [SYS_read]   = do_sys_read,
    [SYS_getpid] = do_sys_getpid,
    [SYS_yield]  = do_sys_yield,
};
