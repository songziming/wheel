// 虚拟终端设备，不断读取 /dev/keyboard，转换为字符串
// 内核态 shell，以 tty-task 的身份运行内核函数

// TODO 定义一套接口，允许其他模块注册 ksh command
//      将 ksh command 组织为红黑树，按 command 字典序排列

// TODO tty 也应该是一个文件？类似 /dev/keyboard，只是内容为可显示文本？

#include <shell.h>
#include <keyboard.h>
#include <wheel.h>


// TODO 数字小键盘尚不支持


// 一些按键的状态
static char kbd_capslock = 0;
static char kbd_numlock  = 1;
static char kbd_l_shift   = 0;
static char kbd_r_shift   = 0;
static char kbd_l_control = 0;
static char kbd_r_control = 0;
static char kbd_l_alt     = 0;
static char kbd_r_alt     = 0;

// 数字键上方的符号（数字 0 排在开头，与键盘布局不同）
static const char syms[] = ")!@#$%^&*(";

// 将按键码转换为 ASCII 字符，如果不是可打印字符，就返回 -1
static char keycode_to_ascii(keycode_t key) {
    int release = key & KEY_RELEASE;
    key &= ~KEY_RELEASE;

    if (release) {
        switch (key) {
        case KEY_LEFTSHIFT:
            kbd_l_shift = 0;
            return -1;
        case KEY_RIGHTSHIFT:
            kbd_r_shift = 0;
            return -1;
        case KEY_LEFTCTRL:
            kbd_l_control = 0;
            return -1;
        case KEY_RIGHTCTRL:
            kbd_r_control = 0;
            return -1;
        case KEY_LEFTALT:
            kbd_l_alt = 0;
            return -1;
        case KEY_RIGHTALT:
            kbd_r_alt = 0;
            return -1;
        default:
            return -1;
        }
    }

    switch (key) {
    // modifiers
    case KEY_LEFTSHIFT:
        kbd_l_shift = 1;
        return -1;
    case KEY_RIGHTSHIFT:
        kbd_r_shift = 1;
        return -1;
    case KEY_LEFTCTRL:
        kbd_l_control = 1;
        return -1;
    case KEY_RIGHTCTRL:
        kbd_r_control = 1;
        return -1;
    case KEY_LEFTALT:
        kbd_l_alt = 1;
        return -1;
    case KEY_RIGHTALT:
        kbd_r_alt = 1;
        return -1;

    // locks
    case KEY_CAPSLOCK:
        kbd_capslock ^= 1;
        return -1;
    case KEY_NUMLOCK:
        kbd_numlock  ^= 1;
        return -1;

    // letters
    case KEY_A: case KEY_B: case KEY_C: case KEY_D: case KEY_E:
    case KEY_F: case KEY_G: case KEY_H: case KEY_I: case KEY_J:
    case KEY_K: case KEY_L: case KEY_M: case KEY_N: case KEY_O:
    case KEY_P: case KEY_Q: case KEY_R: case KEY_S: case KEY_T:
    case KEY_U: case KEY_V: case KEY_W: case KEY_X: case KEY_Y:
    case KEY_Z:
        if (kbd_l_control | kbd_r_control) {
            // TODO: control characters
            return -1;
        }
        if (kbd_l_alt | kbd_r_alt) {
            // TODO: option characters
            return -1;
        }
        if (kbd_capslock ^ (kbd_l_shift | kbd_r_shift)) {
            return 'A' + (key - KEY_A);
        } else {
            return 'a' + (key - KEY_A);
        }

    // numbers
    case KEY_0: case KEY_1: case KEY_2: case KEY_3: case KEY_4:
    case KEY_5: case KEY_6: case KEY_7: case KEY_8: case KEY_9:
        if (kbd_l_control | kbd_r_control) {
            // TODO: control characters
            return -1;
        }
        if (kbd_l_alt | kbd_r_alt) {
            // TODO: option characters
            return -1;
        }
        if (kbd_l_shift | kbd_r_shift) {
            return syms[key - KEY_0];
        } else {
            return '0' + (key - KEY_0);
        }

    case KEY_BACKTICK:
        if (kbd_l_shift | kbd_r_shift) {
            return '~';
        } else {
            return '`';
        }
    case KEY_MINUS:
        if (kbd_l_shift | kbd_r_shift) {
            return '_';
        } else {
            return '-';
        }
    case KEY_EQUAL:
        if (kbd_l_shift | kbd_r_shift) {
            return '+';
        } else {
            return '=';
        }
    case KEY_LEFTBRACE:
        if (kbd_l_shift | kbd_r_shift) {
            return '{';
        } else {
            return '[';
        }
    case KEY_RIGHTBRACE:
        if (kbd_l_shift | kbd_r_shift) {
            return '}';
        } else {
            return ']';
        }
    case KEY_SEMICOLON:
        if (kbd_l_shift | kbd_r_shift) {
            return ':';
        } else {
            return ';';
        }
    case KEY_QUOTE:
        if (kbd_l_shift | kbd_r_shift) {
            return '\"';
        } else {
            return '\'';
        }
    case KEY_COMMA:
        if (kbd_l_shift | kbd_r_shift) {
            return '<';
        } else {
            return ',';
        }
    case KEY_DOT:
        if (kbd_l_shift | kbd_r_shift) {
            return '>';
        } else {
            return '.';
        }
    case KEY_SLASH:
        if (kbd_l_shift | kbd_r_shift) {
            return '?';
        } else {
            return '/';
        }
    case KEY_BACKSLASH:
        if (kbd_l_shift | kbd_r_shift) {
            return '|';
        } else {
            return '\\';
        }

    // whitespace
    case KEY_TAB:
        return '\t';
    case KEY_SPACE:
        return ' ';
    case KEY_ENTER:
        return '\n';

    // unsupported keys
    default:
        return -1;
    }
}


//------------------------------------------------------------------------------
// 内核
//------------------------------------------------------------------------------


static rbtree_t g_cmds = RBTREE_INIT;   // 记录所有命令
static task_t g_shell_tcb;



// 去掉字符串开头的空白，如果全是空白，则返回空指针
static char *trim(char *s) {
    for (; *s; ++s) {
        if ((' ' != *s) && ('\t' != *s)) {
            return s;
        }
    }

    return NULL;
}

// 从字符串里分割出第一个单词，返回剩余部分的字符串
static char *split(char *s) {
    // if (NULL == s) {
    //     return NULL;
    // }

    // // 跳过开头连续的空白
    // for (; *s; ++s) {
    //     if ((' ' != *s) && ('\t' != *s)) {
    //         break;
    //     }
    // }

    // 找到下一个空白
    for (; *s; ++s) {
        if ((' ' == *s) || ('\t' == *s)) {
            *s = '\0';
            ++s;
            break;
        }
    }

    // 返回剩余部分的第一个非空白字符
    return trim(s);
}

// 注册一个命令
INIT_TEXT void shell_add_cmd(shell_cmd_t *cmd) {
    ASSERT(NULL != cmd);

    rbnode_t **link = &g_cmds.root;
    rbnode_t *parent = NULL;

    while (NULL != *link) {
        shell_cmd_t *ref = containerof(*link, shell_cmd_t, rb);
        int diff = strncmp(cmd->name, ref->name, 1024);

        parent = *link;
        if (0 == diff) {
            klog("warning: command %s already exist!\n", cmd->name);
            return;
        } else if (diff < 0) {
            link = &parent->left;
        } else {
            link = &parent->right;
        }
    }

    rb_insert(&g_cmds, &cmd->rb, parent, link);
}

// 中序遍历
static void run_help(rbnode_t *rb) {
    if (NULL == rb) {
        return;
    }

    run_help(rb->left);
    shell_cmd_t *cmd = containerof(rb, shell_cmd_t, rb);
    klog("%s, ", cmd->name);
    run_help(rb->right);
}

// 解析并执行命令
static void execute(char *line) {
    int argc;
    char *argv[32];
    line = trim(line);
    for (argc = 0; (argc < 32) && line; ++argc) {
        argv[argc] = line;
        line = split(line);
    }

    if ((0 == argc) || (NULL == argv[0])) {
        return;
    }

    // 搜索已注册的命令
    rbnode_t *rb = g_cmds.root;
    while (rb) {
        shell_cmd_t *cmd = containerof(rb, shell_cmd_t, rb);
        int diff = strncmp(argv[0], cmd->name, 1024);
        if (0 == diff) {
            cmd->func(argc, argv);
            return;
        }
        if (diff < 0) {
            rb = rb->left;
        } else {
            rb = rb->right;
        }
    }

    // 检查内部命令
    if (!strncmp(argv[0], "help", 10)) {
        klog("commands: ");
        run_help(g_cmds.root);
        klog("help, exit\n");
        return;
    }

    if (!strncmp(argv[0], "exit", 10)) {
        emu_exit(0);
        return;
    }

    klog("error: no command `%s`\n", argv[0]);
}



#define PROMPT "kshell"


// tty 拥有字符输出终端，可以控制显示什么内容
// tty 启动之后应该禁用 klog，只能将字符串发给 /dev/log，logTask 负责不断读取并打印
static void shell_proc() {
    klog("\nkernel shell started\n");
    klog("%s> ", PROMPT);
    char cmd[1024 + 1];
    int len = 0;

    while (1) {
        keycode_t key = keyboard_recv();
        char ch = keycode_to_ascii(key);
        if ((char)-1 == ch) {
            continue;
        }

        klog("%c", ch); // 回显

        if ('\n' == ch) {
            cmd[len] = '\0';
            execute(cmd); // 执行命令
            len = 0;
            klog("%s> ", PROMPT); // 打印下一个 prompt
            continue;
        }

        if (len < 1024) {
            cmd[len++] = ch;
        }
    }

    klog("\nkernel shell exited!\n");
}

// TODO 定义 terminal 设备的接口，成员函数包括打印、清屏、设置光标、设置颜色等
//      由 arch 提供该接口（console、framebuf 均可提供），shell 使用
//      便于实现 inline-editing、history 等功能
// TODO 打印调试输出（klog）也应该换成 terminal 接口的方案
// TODO 可以用不同的颜色区分不同类别的打印
INIT_TEXT void shell_init() {
    task_create(&g_shell_tcb, "shell", 1, shell_proc);
    // g_shell_tcb.affinity = 1;
    task_resume(&g_shell_tcb);
}
