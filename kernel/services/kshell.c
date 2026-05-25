#include "kshell.h"
#include "console.h"
#include <task.h>
#include <kstring.h>
#include <debug.h>


// 使用 console 读取输入、写入输出
// 作为任务运行，只有一个线程，不会重入，因此不用自旋锁

static task_t shell_tcb;
#define PROMPT "kshell"

// defined in arch linker.ld
extern const kcmd_t _kcmd_addr;
extern const kcmd_t _kcmd_end;

static void show_help(int argc UNUSED, char *argv[] UNUSED) {
    console_printf("valid commands:\n");
    for (const kcmd_t *q = &_kcmd_addr; q < &_kcmd_end; ++q) {
        console_printf(" -> %s\n", q->name);
    }
}

KSHELL_CMD("help", show_help);

static void execute(char *cmd) {
    // 分解命令行参数
    char *argv[32];
    int argc = 0;

    // 跳过开头空格
    while (' ' == *cmd) ++cmd;
    if ('\0' == *cmd) {
        return;  // 空行
    }
    argv[argc++] = cmd;

    // 解析剩下的参数
    for (; *cmd; ++cmd) {
        if (' ' == *cmd) {
            *cmd++ = '\0';
            while (' ' == *cmd) { ++cmd; }
            if (*cmd) {
                // 找到了下一个不是空格的字符，说明有下一个参数
                argv[argc++] = cmd;
            }
        }
    }

    console_printf(" -- got %d args\n", argc);
    for (int i = 0; i < argc; ++i) {
        console_printf("  -> arg.%d = %s\n", i, argv[i]);
    }

    for (const kcmd_t *q = &_kcmd_addr; q < &_kcmd_end; ++q) {
        // console_printf("searching cmd %p %s\n", q, q->name);
        if (kstrcmp(q->name, argv[0]) == 0) {
            q->func(argc, argv);
            break;
        }
    }
}

static void kshell_proc() {
    logk("kernel shell started\n");

    char input[1024];

    while (1) {
        console_printf("%s> ", PROMPT);
        console_readline(input, sizeof(input));
        execute(input);
    }
}

INIT_TEXT void kshell_start() {
    task_create(&shell_tcb, "shell", 30, kshell_proc);
    task_start_now(&shell_tcb);
}
