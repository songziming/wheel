#include <wheel.h>
#include <task.h>
#include <console.h>

// called in arch_entries.S

size_t do_syscall(int id, size_t a1, size_t a2, size_t a3, size_t a4) {
    console_printf("handling syscall #%d\n", id);

    if (123 == id) {
        console_printf("print: `%s`\n", (char*)a1);
    }

    if (0 == id) {
        task_exit();
    }

    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;

    return id + 1;
}