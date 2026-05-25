#include "i8042_kbd.h"
#include <arch_api.h>
#include <arch_int.h>
#include <apic/apic.h>
#include <keyboard.h>
#include <debug.h>

// PS/2 keyboard controller

// 我们通过 port io 控制 PS/2 controller（位于主板）
// PS/2 controller 通过串口和键盘通信

#define PS2KBD_CTRL_PORT 0x64 // R:status, W:command
#define PS2KBD_DATA_PORT 0x60


// 将 scan code set1 扫描码转换为 keycode
static const keycode_t SET1_LUT[] = {
    KEY_NONE,   KEY_ESC,      KEY_1,         KEY_2,      // 0x00 - 0x03
    KEY_3,      KEY_4,        KEY_5,         KEY_6,      // 0x04 - 0x07
    KEY_7,      KEY_8,        KEY_9,         KEY_0,
    KEY_MINUS,  KEY_EQUAL,    KEY_BACKSPACE, KEY_TAB,
    KEY_Q,      KEY_W,        KEY_E,         KEY_R,
    KEY_T,      KEY_Y,        KEY_U,         KEY_I,
    KEY_O,      KEY_P,        KEY_L_BRACE,   KEY_R_BRACE,
    KEY_ENTER,  KEY_L_CTRL,   KEY_A,         KEY_S,
    KEY_D,      KEY_F,        KEY_G,         KEY_H,
    KEY_J,      KEY_K,        KEY_L,         KEY_SEMICOLON,
    KEY_QUOTE,  KEY_BACKTICK, KEY_L_SHIFT,   KEY_BACKSLASH,
    KEY_Z,      KEY_X,        KEY_C,         KEY_V,
    KEY_B,      KEY_N,        KEY_M,         KEY_COMMA,
    KEY_DOT,    KEY_SLASH,    KEY_R_SHIFT,   KEY_KP_STAR,
    KEY_L_ALT,  KEY_SPACE,    KEY_CAPSLOCK,  KEY_F1,
    KEY_F2,     KEY_F3,       KEY_F4,        KEY_F5,
    KEY_F6,     KEY_F7,       KEY_F8,        KEY_F9,
    KEY_F10,    KEY_NUMLOCK,  KEY_SCRLOCK,   KEY_KP_7,
    KEY_KP_8,   KEY_KP_9,     KEY_KP_MINUS,  KEY_KP_4,
    KEY_KP_5,   KEY_KP_6,     KEY_KP_PLUS,   KEY_KP_1,
    KEY_KP_2,   KEY_KP_3,     KEY_KP_0,      KEY_KP_DOT,
    KEY_NONE,   KEY_NONE,     KEY_INTL,      KEY_F11,    // 0x54 - 0x57
    KEY_F12,    KEY_NONE,     KEY_NONE,      KEY_NONE,   // 0x58 - 0x5b
    KEY_NONE,   KEY_NONE,     KEY_NONE,      KEY_NONE,   // 0x5c - 0x5f
    KEY_NONE,   KEY_NONE,     KEY_NONE,      KEY_NONE,   // 0x60 - 0x63
    KEY_F13,    KEY_F14,      KEY_F15,       KEY_F16,    // 0x64 - 0x67
    KEY_F17,    KEY_F18,      KEY_F19,       KEY_F20,    // 0x68 - 0x6b
    KEY_F21,    KEY_F22,      KEY_F23,       KEY_NONE,   // 0x6c - 0x6f
    KEY_NONE,   KEY_NONE,     KEY_NONE,      KEY_NONE,   // 0x70 - 0x73
    KEY_NONE,   KEY_NONE,     KEY_F24,       KEY_NONE,   // 0x74 - 0x77
};


// 某些按键对应多个扫描码，需要为扫描码前缀定义状态码
// 解析器是个状态机，解析过程就是状态转换过程
#define STATE_NORMAL        0   // 无前缀
#define STATE_E0            1   // prefix e0
#define STATE_PRTSC_DOWN_2  2   // print screen pressed, got e0, 2a
#define STATE_PRTSC_DOWN_3  3   // print screen pressed, got e0, 2a, e0
#define STATE_PRTSC_UP_2    4   // print screen released, got e0, b7
#define STATE_PRTSC_UP_3    5   // print screen released, got e0, b7, e0
#define STATE_PAUSE_1       6   // pause sequence, got e1
#define STATE_PAUSE_2       7   // pause sequence, got e1, 1d
#define STATE_PAUSE_3       8   // pause sequence, got e1, 1d, 45
#define STATE_PAUSE_4       9   // pause sequence, got e1, 1d, 45, e1
#define STATE_PAUSE_5       10  // pause sequence, got e1, 1d, 45, e1, 9d

// 只有 ISR 可以解析 scancode，更新状态机，所以不需要自旋锁
static int g_state = STATE_NORMAL;

static void handle_keycode(keycode_t key, int release) {
    if (release) {
        send_keycode(key | KEY_RELEASE);
    } else {
        send_keycode(key);
    }
}

// 输入 scancode，将其转换成可打印字符
static void handle_scancode(uint8_t code) {
    switch (g_state) {
    case STATE_NORMAL:
        if (0xe0 == code) {
            g_state = STATE_E0;
            break;
        }
        if (0xe1 == code) { // must be pause_sequence
            g_state = STATE_PAUSE_1;
            break;
        }
        if ((code & 0x7f) < sizeof(SET1_LUT) / sizeof(SET1_LUT[0])) {
            handle_keycode(SET1_LUT[code & 0x7f], code & 0x80);
        }
        break;
    case STATE_E0:
        if (0x2a == code) {
            g_state = STATE_PRTSC_DOWN_2; // print screen 按下
            break;
        }
        if (0xb7 == code) {
            g_state = STATE_PRTSC_UP_2; // print screen 抬起
            break;
        }
        switch (code & 0x7f) {
        case 0x10: handle_keycode(KEY_MM_PREV,       code & 0x80); break;
        case 0x19: handle_keycode(KEY_MM_NEXT,       code & 0x80); break;
        case 0x1c: handle_keycode(KEY_KP_ENTER,      code & 0x80); break;
        case 0x1d: handle_keycode(KEY_R_CTRL,        code & 0x80); break;
        case 0x20: handle_keycode(KEY_MM_MUTE,       code & 0x80); break;
        case 0x21: handle_keycode(KEY_MM_CALC,       code & 0x80); break;
        case 0x22: handle_keycode(KEY_MM_PLAY,       code & 0x80); break;
        case 0x24: handle_keycode(KEY_MM_STOP,       code & 0x80); break;
        case 0x2e: handle_keycode(KEY_MM_VOLDOWN,    code & 0x80); break;
        case 0x30: handle_keycode(KEY_MM_VOLUP,      code & 0x80); break;
        case 0x32: handle_keycode(KEY_WWW_HOME,      code & 0x80); break;
        case 0x35: handle_keycode(KEY_KP_SLASH,      code & 0x80); break;
        case 0x38: handle_keycode(KEY_R_ALT,         code & 0x80); break;
        case 0x47: handle_keycode(KEY_HOME,          code & 0x80); break;
        case 0x48: handle_keycode(KEY_UP,            code & 0x80); break;
        case 0x49: handle_keycode(KEY_PAGEUP,        code & 0x80); break;
        case 0x4b: handle_keycode(KEY_LEFT,          code & 0x80); break;
        case 0x4d: handle_keycode(KEY_RIGHT,         code & 0x80); break;
        case 0x4f: handle_keycode(KEY_END,           code & 0x80); break;
        case 0x50: handle_keycode(KEY_DOWN,          code & 0x80); break;
        case 0x51: handle_keycode(KEY_PAGEDOWN,      code & 0x80); break;
        case 0x52: handle_keycode(KEY_INSERT,        code & 0x80); break;
        case 0x53: handle_keycode(KEY_DELETE,        code & 0x80); break;
        case 0x57: handle_keycode(KEY_F11,           code & 0x80); break;
        case 0x58: handle_keycode(KEY_F12,           code & 0x80); break;
        case 0x5b: handle_keycode(KEY_GUI_LEFT,      code & 0x80); break;
        case 0x5c: handle_keycode(KEY_GUI_RIGHT,     code & 0x80); break;
        case 0x5d: handle_keycode(KEY_APPS,          code & 0x80); break;
        case 0x5e: handle_keycode(KEY_ACPI_POWER,    code & 0x80); break;
        case 0x5f: handle_keycode(KEY_ACPI_SLEEP,    code & 0x80); break;
        case 0x63: handle_keycode(KEY_ACPI_WAKE,     code & 0x80); break;
        case 0x65: handle_keycode(KEY_WWW_SEARCH,    code & 0x80); break;
        case 0x66: handle_keycode(KEY_WWW_FAVORITES, code & 0x80); break;
        case 0x67: handle_keycode(KEY_WWW_REFRESH,   code & 0x80); break;
        case 0x68: handle_keycode(KEY_WWW_STOP,      code & 0x80); break;
        case 0x69: handle_keycode(KEY_WWW_FORWARD,   code & 0x80); break;
        case 0x6a: handle_keycode(KEY_WWW_BACK,      code & 0x80); break;
        case 0x6b: handle_keycode(KEY_MM_MYCOMPUTER, code & 0x80); break;
        case 0x6c: handle_keycode(KEY_MM_EMAIL,      code & 0x80); break;
        case 0x6d: handle_keycode(KEY_MM_SELECT,     code & 0x80); break;
        }
        g_state = STATE_NORMAL;
        break;
    case STATE_PRTSC_DOWN_2:
        if (0xe0 == code) {
            g_state = STATE_PRTSC_DOWN_3;
        } else {
            g_state = STATE_NORMAL;
        }
        break;
    case STATE_PRTSC_UP_2:
        if (0xe0 == code) {
            g_state = STATE_PRTSC_UP_3;
        } else {
            g_state = STATE_NORMAL;
        }
        break;
    case STATE_PRTSC_DOWN_3:
        if (0x37 == code) {
            handle_keycode(KEY_PRTSC, 0); // 按下
        }
        g_state = STATE_NORMAL;
        break;
    case STATE_PRTSC_UP_3:
        if (0xaa == code) {
            handle_keycode(KEY_PRTSC, 1); // 抬起
        }
        g_state = STATE_NORMAL;
        break;
    case STATE_PAUSE_1:
        if (0x1d == code) {
            g_state = STATE_PAUSE_2;
        } else {
            g_state = STATE_NORMAL;
        }
        break;
    case STATE_PAUSE_2:
        if (0x45 == code) {
            g_state = STATE_PAUSE_3;
        } else {
            g_state = STATE_NORMAL;
        }
        break;
    case STATE_PAUSE_3:
        if (0xe1 == code) {
            g_state = STATE_PAUSE_4;
        } else {
            g_state = STATE_NORMAL;
        }
        break;
    case STATE_PAUSE_4:
        if (0x9d == code) {
            g_state = STATE_PAUSE_5;
        } else {
            g_state = STATE_NORMAL;
        }
        break;
    case STATE_PAUSE_5:
        if (0xc5 == code) {
            handle_keycode(KEY_PAUSE, 0);
        }
        g_state = STATE_NORMAL;
        break;
    default:
        break;
    }
}

static void handle_kbd() {
    while (in8(PS2KBD_CTRL_PORT) & 1) {
        handle_scancode(in8(PS2KBD_DATA_PORT));
    }

    // 一次中断结束
    ASSERT(STATE_NORMAL == g_state);
    loapic_send_eoi();
}

// TODO 轮询 ready bit 需要有超时时间

// 向 PS/2 controller 发送命令，并接受反馈
static void i8042_command(uint8_t cmd) {
    while (in8(PS2KBD_CTRL_PORT) & 2) {
        cpu_pause();
    }
    out8(PS2KBD_CTRL_PORT, cmd);
}

// 向 PS/2 controller 发送数据
static void i8042_data(uint8_t data) {
    while (in8(PS2KBD_CTRL_PORT) & 2) {
        cpu_pause();
    }
    out8(PS2KBD_DATA_PORT, data);
}

// 从 PS/2 controller 读取数据
static uint8_t i8042_read() {
    while (0 == (in8(PS2KBD_CTRL_PORT) & 1)) {
        cpu_pause();
    }
    return in8(PS2KBD_DATA_PORT);
}

INIT_TEXT void i8042_init() {
    // PC 一般都有 8042 兼容设备，我们默认存在

    i8042_command(0xad); // 关闭第一个 PS/2 端口（键盘）
    i8042_command(0xa7); // 关闭第二个 PS/2 端口（鼠标）

    // 不断读取缓冲区
    while (in8(PS2KBD_CTRL_PORT) & 1) {
        in8(PS2KBD_DATA_PORT);
    }

    // 配置
    i8042_command(0x20); // read config
    uint8_t cfg = i8042_read();
    cfg &= ~(1 << 0);   // disable port 1 IRQ
    cfg &= ~(1 << 4);   // enable port 1 clock
    cfg &= ~(1 << 6);   // disable port 1 translation
    i8042_command(0x60); // write config
    i8042_data(cfg);

    // 自检
    i8042_command(0xaa);
    uint8_t chk = i8042_read();
    if (0x55 != chk) {
        logk("warning: i8042 self-test failed, expect 0x55, got 0x%x\n", chk);
    }

    // 判断 channel 2 是否存在
    i8042_command(0xa8); // try enable channel 2
    i8042_command(0x20); // read config
    cfg = i8042_read();
    int dual_channel = 0 == (cfg & (1 << 5));
    if (dual_channel) {
        logk("PS/2 has channel 2\n");
        i8042_command(0xa7); // disable channel 2
        cfg &= ~(1 << 1); // disable port 2 IRQ
        cfg &= ~(1 << 5); // enable port 2 clock
        i8042_command(0x60); // write config
        i8042_data(cfg);
    }

    // 重新打开 channel 1
    cfg |= 1 << 0;  // enable keyboard IRQ
    cfg |= 1 << 6;  // enable translation (to scan code set 1)
    i8042_command(0x60); // write config
    i8042_data(cfg);

    // IBM-PC 键盘中断连接到 IRQ 1
    int gsi = g_irq_to_gsi[1];
    irq_handlers[VEC_GSI_BASE + gsi] = handle_kbd;
    ioapic_unmask_gsi(gsi);

    // 启用键盘端口
    i8042_command(0xae);

#if 0
    // 重置 PS/2 device（注意不是重置 controller）
    logk("resetting PS2 keyboard\n");
    i8042_data(0xff);
    uint8_t rsp1 = i8042_read();
    uint8_t rsp2 = i8042_read();
    uint8_t id = i8042_read();
    logk("rsponse = %02x,%02x, dev id = %02x\n", rsp1, rsp2, id);
#endif
}

// TODO 还需要其他键盘函数，例如设置 LED 状态、设置重复按键时间
