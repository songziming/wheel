// 虚拟终端设备，不断读取 /dev/keyboard，转换为字符串

// TODO tty 也应该是一个文件？类似 /dev/keyboard，只是内容为可显示文本？

#include <keyboard.h>

#include <wheel.h>


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







// tty 拥有字符输出终端，可以控制显示什么内容
// tty 启动之后应该禁用 klog，只能将字符串发给 /dev/log，logTask 负责不断读取并打印
static void tty_proc() {
    klog("tty starting...\n");

    while (1) {
        keycode_t key = keyboard_recv();
        char ch = keycode_to_ascii(key);
        if ((char)-1 == ch) {
            continue;
        }

        klog("%c", ch);
    }
}

static task_t g_tty_tcb;

INIT_TEXT void tty_init() {
    task_create(&g_tty_tcb, "tty", 1, tty_proc);
    task_resume(&g_tty_tcb);
}
