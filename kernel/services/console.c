#include "console.h"
#include "keyboard.h"
#include <kstring.h>
#include <format.h>
#include <spin.h>
#include <debug.h>


// 终端，管理输入输出
// 使用者调用 read，则不断读取键盘输入，解析为 ascii，返回调用者
// 输出函数可以处理 escape-sequence，可以设置光标位置、修改字体颜色

//------------------------------------------------------------------------------
// 终端输出
//------------------------------------------------------------------------------

CONST display_ops_t *g_display = NULL;
static spin_t console_lock = SPIN_INIT;
static int caret_x = 0;
static int caret_y = 0;

static void _console_puts(const char *s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        switch (s[i]) {
        case '\t':
            caret_x += 8;
            caret_x &= ~7;
            break;
        case '\n':
            ++caret_y;
            // fallthrough
        case '\r':
            caret_x = 0;
            break;
        case '\b':
            if (caret_x > 0) {
                --caret_x;
            }
            break;
        default:
            g_display->draw_char(s[i], caret_x, caret_y);
            ++caret_x;
            break;
        }

        if (caret_x >= g_display->width) {
            caret_x = 0;
            ++caret_y;
        }
        if (caret_y >= g_display->height) {
            g_display->scroll(caret_y - g_display->height + 1);
            caret_y = g_display->height - 1;
        }
    }
}

void console_puts(const char *s, size_t n) {
    int key = irq_spin_take(&console_lock);
    g_display->draw_char(' ', caret_x, caret_y); // 清除当前光标
    _console_puts(s, n);
    g_display->draw_caret(caret_x, caret_y); // 绘制新的光标
    irq_spin_give(&console_lock, key);
}

static void print_flush(void *user UNUSED, const char **s, size_t *len) {
    _console_puts(*s, *len);
}

// 类似 logk，但是向屏幕打印，而不是串口
// 正常输出文本，而不是编辑
void console_printf(const char *fmt, ...) {
    char tmp[256];

    int key = irq_spin_take(&console_lock);
    g_display->draw_char(' ', caret_x, caret_y); // 清除前一次的光标

    va_list va;
    va_start(va, fmt);
    format(tmp, sizeof(tmp), print_flush, NULL, fmt, va);
    va_end(va);

    g_display->draw_caret(caret_x, caret_y); // 绘制光标
    irq_spin_give(&console_lock, key);
}


//------------------------------------------------------------------------------
// 键盘输入
//------------------------------------------------------------------------------

// 键盘状态
static struct {
    unsigned capslock   : 1;
    unsigned numlock    : 1;
    unsigned scrlock    : 1;
    //
    unsigned l_shift    : 1;
    unsigned r_shift    : 1;
    unsigned l_ctrl     : 1;
    unsigned r_ctrl     : 1;
    unsigned l_alt      : 1;
    unsigned r_alt      : 1;
} kbd_state;

// 数字键上方的符号（数字 0 排在开头，与键盘布局不同）
static const char SYMBOLS[] = ")!@#$%^&*(";

// 得到按键事件，更新键盘状态
static char handle_keycode(keycode_t key) {
    int release = key & KEY_RELEASE;
    key &= ~KEY_RELEASE;

    if (release) {
        switch (key) {
        case KEY_L_SHIFT:   kbd_state.l_shift = 0;  return -1;
        case KEY_R_SHIFT:   kbd_state.r_shift = 0;  return -1;
        case KEY_L_CTRL:    kbd_state.l_ctrl = 0;   return -1;
        case KEY_R_CTRL:    kbd_state.r_ctrl = 0;   return -1;
        case KEY_L_ALT:     kbd_state.l_alt = 0;    return -1;
        case KEY_R_ALT:     kbd_state.r_alt = 0;    return -1;
        default: return -1;
        }
    }

    switch (key) {
    // modifiers
    case KEY_L_SHIFT:   kbd_state.l_shift = 1;  return -1;
    case KEY_R_SHIFT:   kbd_state.r_shift = 1;  return -1;
    case KEY_L_CTRL:    kbd_state.l_ctrl = 1;   return -1;
    case KEY_R_CTRL:    kbd_state.r_ctrl = 1;   return -1;
    case KEY_L_ALT:     kbd_state.l_alt = 1;    return -1;
    case KEY_R_ALT:     kbd_state.r_alt = 1;    return -1;

    // locks
    case KEY_CAPSLOCK:  kbd_state.capslock ^= 1;    return -1;
    case KEY_NUMLOCK:   kbd_state.numlock  ^= 1;    return -1;
    case KEY_SCRLOCK:   kbd_state.scrlock  ^= 1;    return -1;

    // letters
    case KEY_A: case KEY_B: case KEY_C: case KEY_D: case KEY_E:
    case KEY_F: case KEY_G: case KEY_H: case KEY_I: case KEY_J:
    case KEY_K: case KEY_L: case KEY_M: case KEY_N: case KEY_O:
    case KEY_P: case KEY_Q: case KEY_R: case KEY_S: case KEY_T:
    case KEY_U: case KEY_V: case KEY_W: case KEY_X: case KEY_Y:
    case KEY_Z:
        if (kbd_state.l_ctrl | kbd_state.r_ctrl) {
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
        if (kbd_state.l_ctrl | kbd_state.r_ctrl) {
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
    case KEY_L_BRACE:
        if (kbd_state.l_shift | kbd_state.r_shift) {
            return '{';
        } else {
            return '[';
        }
    case KEY_R_BRACE:
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
    case KEY_SPACE:     return ' ';
    case KEY_TAB:       return '\t';
    case KEY_ENTER:     return '\n';
    case KEY_BACKSPACE: return '\b';

    // unsupported keys
    default: break;
    }

    // handle keypad
    if (kbd_state.numlock) {
        switch (key) {
        case KEY_KP_0: case KEY_KP_1: case KEY_KP_2: case KEY_KP_3: case KEY_KP_4:
        case KEY_KP_5: case KEY_KP_6: case KEY_KP_7: case KEY_KP_8: case KEY_KP_9:
            return '0' + (key - KEY_KP_0);
        case KEY_KP_SLASH:  return '/';
        case KEY_KP_STAR:   return '*';
        case KEY_KP_MINUS:  return '-';
        case KEY_KP_PLUS:   return '+';
        case KEY_KP_DOT:    return '.';
        case KEY_KP_ENTER:  return '\n';
        default: break;
        }
    }

    return -1;
}

// 返回一个按键，但也要处理，否则无法响应 capslock、shift 这类状态
keycode_t console_readraw() {
    keycode_t kc = get_keycode();
    handle_keycode(kc);
    return kc;
}

// 读取一个完整字符串，一行，输入回车则停止，字符串不含换行
// 如果按下tab，自动转换成多个空格，不允许滚屏
void console_readline(char *dst, size_t max) {
    size_t limit = g_display->width * 10; // 最多允许读取 10 行
    if (max > limit) {
        max = limit;
    }

    size_t len = 0; // 已经读取多少字符
    while (len < max - 1) {
        keycode_t kc = get_keycode();
        char c = handle_keycode(kc);

        // 光标也是画出来的，回显之前需要首先清除 caret
        g_display->draw_char(' ', caret_x, caret_y);

        if ('\n' == c) {
            ++caret_y;
            caret_x = 0;
        } else if ('\t' == c) {
            int nspaces = 8 - (caret_x & 7);
            for (; nspaces && (len < max - 1); --nspaces, ++len, ++caret_x) {
                dst[len] = ' ';
            }
        } else if ('\b' == c) {
            if (len > 0) {
                --len;
                --caret_x;
                if (caret_x < 0) {
                    caret_x = g_display->width - 1;
                    --caret_y;
                }
                g_display->draw_char(' ', caret_x, caret_y); // 清除字符
            }
        } else if (-1 != c) {
            dst[len] = c;
            g_display->draw_char(c, caret_x, caret_y);
            ++len;
            ++caret_x;
        }

        if (caret_x >= g_display->width) {
            caret_x = 0;
            ++caret_y;
        }
        if (caret_y >= g_display->height) {
            g_display->scroll(caret_y - g_display->height + 1);
            caret_y = g_display->height - 1;
        }

        g_display->draw_caret(caret_x, caret_y);
        if ('\n' == c) {
            break;
        }
    }

    dst[len] = '\0';
}

//------------------------------------------------------------------------------
// 初始化
//------------------------------------------------------------------------------

INIT_TEXT void console_init() {
    kmemset(&kbd_state, 0, sizeof(kbd_state));
    kbd_state.numlock = 1; // 默认开启小键盘
}

static size_t parse_num(const char *s, int base) {
    size_t n = 0;
    for (; *s; ++s) {
        n *= base;
        if (('a' <= *s) && (*s <= 'f')) {
            n += *s - 'a' + 10;
        } else if (('A' <= *s) && (*s <= 'F')) {
            n += *s - 'A' + 10;
        } else if (('0' <= *s) && (*s <= '9')) {
            n += *s - '0';
        } else {
            break;
        }
    }
    return n;
}

size_t str2num(const char *s) {
    if (s[0] != '0') {
        return parse_num(s, 10);
    }
    if (s[1] == 'x' || s[1] == 'X') {
        return parse_num(s + 2, 16);
    }
    if (s[1] == 'b' || s[1] == 'B') {
        return parse_num(s + 2, 2);
    }
    return parse_num(s + 1, 8);
}