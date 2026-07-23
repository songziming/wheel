#include "syscall.h"
#include <wheel.h>
#include <task.h>
#include <proc.h>
#include <console.h>

// 由 arch_entries.S 的 syscall_entry 调用
// 遵循 x86_64 syscall ABI：num 来自 rax, a1–a6 来自 rdi,rsi,rdx,r10,r8,r9
int64_t do_syscall(int64_t num,
        int64_t a1, int64_t a2, int64_t a3,
        int64_t a4, int64_t a5, int64_t a6) {
    console_printf("handling syscall %zd\n", num);

    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;

    switch (num) {
    case SYS_exit:
        task_exit();
        return 0;

    case 123:
        console_printf("print: `%s`\n", (char*)a1);
        return 0;

    case SYS_write:
        console_printf("%s", (const char*)a1);
        // return (int64_t)a3;
        return 0;

    case SYS_yield:
        arch_task_switch();
        return 0;

    case SYS_getpid: {
        task_t *self = current_task();
        if (self->process) {
            return ((proc_t*)self->process)->id;
        }
        return 0;
    }

    default:
        console_printf("syscall #%d (a1=%zx, a2=%zx, a3=%zx)\n",
            (int)num, a1, a2, a3);
        return -1;
    }
}
