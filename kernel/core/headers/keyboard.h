#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <def.h>

typedef enum keycode {
    KEY_RESERVED,

    // 数字
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
    KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,

    // 字母
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G,
    KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N,
    KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,

    // 功能键
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10,KEY_F11,KEY_F12,

    // modifiers
    KEY_LEFTCTRL,   KEY_RIGHTCTRL,
    KEY_LEFTSHIFT,  KEY_RIGHTSHIFT,
    KEY_LEFTALT,    KEY_RIGHTALT,

    // 标点符号
    KEY_BACKTICK,   KEY_MINUS,      KEY_EQUAL,      KEY_TAB,
    KEY_LEFTBRACE,  KEY_RIGHTBRACE, KEY_SEMICOLON,  KEY_QUOTE,
    KEY_COMMA,      KEY_DOT,        KEY_SLASH,      KEY_BACKSLASH,
    KEY_SPACE,      KEY_BACKSPACE,  KEY_ENTER,

    // control keys
    KEY_ESC,        KEY_CAPSLOCK,   KEY_NUMLOCK,    KEY_SCROLLLOCK,
    KEY_INSERT,     KEY_DELETE,     KEY_HOME,       KEY_END,
    KEY_PAGEUP,     KEY_PAGEDOWN,
    KEY_UP,         KEY_DOWN,       KEY_LEFT,       KEY_RIGHT,

    // 小键盘区
    KEY_KP_0,       KEY_KP_1,       KEY_KP_2,       KEY_KP_3,       KEY_KP_4,
    KEY_KP_5,       KEY_KP_6,       KEY_KP_7,       KEY_KP_8,       KEY_KP_9,
    KEY_KP_SLASH,   KEY_KP_STAR,    KEY_KP_MINUS,   KEY_KP_PLUS,
    KEY_KP_ENTER,   KEY_KP_DOT,

    // special
    KEY_PRTSC,      KEY_PAUSE,      KEY_APPS,

    // multimedia
    KEY_MM_PREV,    KEY_MM_NEXT,    KEY_MM_PLAY,    KEY_MM_STOP,
    KEY_MM_MUTE,    KEY_MM_CALC,    KEY_MM_VOLUP,   KEY_MM_VOLDOWN,
    KEY_MM_EMAIL,   KEY_MM_SELECT,  KEY_MM_MYCOMPUTER,

    // multimedia www
    KEY_WWW_HOME,   KEY_WWW_SEARCH, KEY_WWW_FAVORITES,
    KEY_WWW_FORWARD,KEY_WWW_BACK,   KEY_WWW_STOP,   KEY_WWW_REFRESH,

    // GUI (windows key)
    KEY_GUI_LEFT,   KEY_GUI_RIGHT,

    // ACPI
    KEY_ACPI_POWER, KEY_ACPI_SLEEP, KEY_ACPI_WAKE,
} keycode_t;

// 最高位表示按键释放
#define KEY_RELEASE 0x80000000U

void keyboard_send(keycode_t key);


#endif // KEYBOARD_H
