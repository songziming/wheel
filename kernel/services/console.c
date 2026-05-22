#include "console.h"
#include "keyboard.h"
#include <kstring.h>

// WIP
// 终端，管理输入输出
// 使用者调用 read，则不断读取键盘输入，解析为 ascii，返回调用者
// 输出函数可以处理 escape-sequence，可以设置光标位置、修改字体颜色


// 键盘状态
static struct {
    unsigned capslock  : 1;
    unsigned numlock   : 1;
    unsigned l_shift   : 1;
    unsigned r_shift   : 1;
    unsigned l_control : 1;
    unsigned r_control : 1;
    unsigned l_alt     : 1;
    unsigned r_alt     : 1;
} kbd_state;

// 数字键上方的符号（数字 0 排在开头，与键盘布局不同）
static const char SYMBOLS[] = ")!@#$%^&*(";

// 得到按键事件，更新键盘状态
static char handle_keycode(keycode_t key) {
    int release = key & KEY_RELEASE;
    key &= ~KEY_RELEASE;

    if (release) {
        switch (key) {
        case KEY_LEFTSHIFT:     kbd_state.l_shift = 0;      return -1;
        case KEY_RIGHTSHIFT:    kbd_state.r_shift = 0;      return -1;
        case KEY_LEFTCTRL:      kbd_state.l_control = 0;    return -1;
        case KEY_RIGHTCTRL:     kbd_state.r_control = 0;    return -1;
        case KEY_LEFTALT:       kbd_state.l_alt = 0;        return -1;
        case KEY_RIGHTALT:      kbd_state.r_alt = 0;        return -1;
        default: return -1;
        }
    }

    switch (key) {
    // modifiers
    case KEY_LEFTSHIFT:     kbd_state.l_shift = 1;      return -1;
    case KEY_RIGHTSHIFT:    kbd_state.r_shift = 1;      return -1;
    case KEY_LEFTCTRL:      kbd_state.l_control = 1;    return -1;
    case KEY_RIGHTCTRL:     kbd_state.r_control = 1;    return -1;
    case KEY_LEFTALT:       kbd_state.l_alt = 1;        return -1;
    case KEY_RIGHTALT:      kbd_state.r_alt = 1;        return -1;

    // locks
    case KEY_CAPSLOCK:      kbd_state.capslock ^= 1;    return -1;
    case KEY_NUMLOCK:       kbd_state.numlock  ^= 1;    return -1;

    // letters
    case KEY_A: case KEY_B: case KEY_C: case KEY_D: case KEY_E:
    case KEY_F: case KEY_G: case KEY_H: case KEY_I: case KEY_J:
    case KEY_K: case KEY_L: case KEY_M: case KEY_N: case KEY_O:
    case KEY_P: case KEY_Q: case KEY_R: case KEY_S: case KEY_T:
    case KEY_U: case KEY_V: case KEY_W: case KEY_X: case KEY_Y:
    case KEY_Z:
        if (kbd_state.l_control | kbd_state.r_control) {
            // TODO: control characters
            return -1;
        }
        if (kbd_state.l_alt | kbd_state.r_alt) {
            // TODO: option characters
            return -1;
        }
        if (kbd_state.capslock ^ (kbd_state.l_shift | kbd_state.r_shift)) {
            return 'A' + (key - KEY_A);
        } else {
            return 'a' + (key - KEY_A);
        }

    // numbers
    case KEY_0: case KEY_1: case KEY_2: case KEY_3: case KEY_4:
    case KEY_5: case KEY_6: case KEY_7: case KEY_8: case KEY_9:
        if (kbd_state.l_control | kbd_state.r_control) {
            // TODO: control characters
            return -1;
        }
        if (kbd_state.l_alt | kbd_state.r_alt) {
            // TODO: option characters
            return -1;
        }
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return SYMBOLS[key - KEY_0];
        } else {
            return '0' + (key - KEY_0);
        }

    case KEY_BACKTICK:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return '~';
        } else {
            return '`';
        }
    case KEY_MINUS:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return '_';
        } else {
            return '-';
        }
    case KEY_EQUAL:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return '+';
        } else {
            return '=';
        }
    case KEY_LEFTBRACE:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return '{';
        } else {
            return '[';
        }
    case KEY_RIGHTBRACE:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return '}';
        } else {
            return ']';
        }
    case KEY_SEMICOLON:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return ':';
        } else {
            return ';';
        }
    case KEY_QUOTE:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return '\"';
        } else {
            return '\'';
        }
    case KEY_COMMA:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return '<';
        } else {
            return ',';
        }
    case KEY_DOT:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return '>';
        } else {
            return '.';
        }
    case KEY_SLASH:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return '?';
        } else {
            return '/';
        }
    case KEY_BACKSLASH:
        if (kbd_state.l_shift | kbd_state.r_shift) {
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

INIT_TEXT void tty_init() {
    kmemset(&kbd_state, 0, sizeof(kbd_state));
    kbd_state.numlock = 1; // 默认开启小键盘
}

void console_readline() {
    // TODO 需要主动读取才会 get-keycode
    //      否则键盘状态无法更新
}

void console_printf() {}
