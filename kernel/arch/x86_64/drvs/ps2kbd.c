#include <wheel.h>

// driver only need to convert scan code to key code
// kbd-task then translate to ascii or unicode

#define PS2KBD_CTRL_PORT 0x64
#define PS2KBD_DATA_PORT 0x60

// lookup table to convert scan code set 1 to key code
static keycode_t normal_lookup[] = {
    KEY_RESERVED,   KEY_ESCAPE,     KEY_1,          KEY_2,          // 0x00 - 0x03
    KEY_3,          KEY_4,          KEY_5,          KEY_6,          // 0x04 - 0x07
    KEY_7,          KEY_8,          KEY_9,          KEY_0,
    KEY_MINUS,      KEY_EQUAL,      KEY_BACKSPACE,  KEY_TAB,
    KEY_Q,          KEY_W,          KEY_E,          KEY_R,
    KEY_T,          KEY_Y,          KEY_U,          KEY_I,
    KEY_O,          KEY_P,          KEY_LEFTBRACE,  KEY_RIGHTBRACE,
    KEY_ENTER,      KEY_LEFTCTRL,   KEY_A,          KEY_S,
    KEY_D,          KEY_F,          KEY_G,          KEY_H,
    KEY_J,          KEY_K,          KEY_L,          KEY_SEMICOLON,
    KEY_QUOTE,      KEY_BACKTICK,   KEY_LEFTSHIFT,  KEY_BACKSLASH,
    KEY_Z,          KEY_X,          KEY_C,          KEY_V,
    KEY_B,          KEY_N,          KEY_M,          KEY_COMMA,
    KEY_DOT,        KEY_SLASH,      KEY_RIGHTSHIFT, KEY_KP_STAR,
    KEY_LEFTALT,    KEY_SPACE,      KEY_CAPSLOCK,   KEY_F1,
    KEY_F2,         KEY_F3,         KEY_F4,         KEY_F5,
    KEY_F6,         KEY_F7,         KEY_F8,         KEY_F9,
    KEY_F10,        KEY_NUMLOCK,    KEY_SCROLLLOCK, KEY_KP_7,
    KEY_KP_8,       KEY_KP_9,       KEY_KP_MINUS,   KEY_KP_4,
    KEY_KP_5,       KEY_KP_6,       KEY_KP_PLUS,    KEY_KP_1,
    KEY_KP_2,       KEY_KP_3,       KEY_KP_0,       KEY_KP_DOT,
    KEY_RESERVED,   KEY_RESERVED,   KEY_RESERVED,   KEY_F11,
    KEY_F12,        KEY_RESERVED,   KEY_RESERVED,   KEY_RESERVED,   // 0x58 - 0x5b
};

// some key have multi-byte scan code
// so this driver also has multiple state, one for each prefix

#define STATE_NORMAL        0
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

static int state = 0;

// // send key code to kbd-task
// static void send_keycode(keycode_t code, int release) {
//     if (release) {
//         code |= 0x80000000;
//     }
//     u32 key = irq_spin_take(&kbd_spin);
//     fifo_write(&kbd_fifo, (u8 *) &code, sizeof(keycode_t), NO);
//     irq_spin_give(&kbd_spin, key);

//     sema_give(&kbd_sema);
// }

#define send_keycode(code, release) kbd_send((release) ? ((code) | 0x80000000) : (code))

// convert from scan code set 1 to key code
static void digest_scan_code(u8 scancode) {
    switch (state) {
    case STATE_NORMAL:
        if (0xe0 == scancode) {
            state = STATE_E0;
            break;
        }
        if (0xe1 == scancode) {     // must be pause_sequence
            state = STATE_PAUSE_1;
            break;
        }
        send_keycode(normal_lookup[scancode & 0x7f], scancode & 0x80);
        break;
    case STATE_E0:
        if (0x2a == scancode) {
            // print screen press sequence
            state = STATE_PRTSC_DOWN_2;
            break;
        }
        if (0xb7 == scancode) {
            // print screen release requence
            state = STATE_PRTSC_UP_2;
            break;
        }
        switch (scancode & 0x7f) {
        case 0x10:  send_keycode(KEY_MM_PREV,       scancode & 0x80);   break;
        case 0x19:  send_keycode(KEY_MM_NEXT,       scancode & 0x80);   break;
        case 0x1c:  send_keycode(KEY_KP_ENTER,      scancode & 0x80);   break;
        case 0x1d:  send_keycode(KEY_RIGHTCTRL,     scancode & 0x80);   break;
        case 0x20:  send_keycode(KEY_MM_MUTE,       scancode & 0x80);   break;
        case 0x21:  send_keycode(KEY_MM_CALC,       scancode & 0x80);   break;
        case 0x22:  send_keycode(KEY_MM_PLAY,       scancode & 0x80);   break;
        case 0x24:  send_keycode(KEY_MM_STOP,       scancode & 0x80);   break;
        case 0x2e:  send_keycode(KEY_MM_VOLDOWN,    scancode & 0x80);   break;
        case 0x30:  send_keycode(KEY_MM_VOLUP,      scancode & 0x80);   break;
        case 0x32:  send_keycode(KEY_WWW_HOME,      scancode & 0x80);   break;
        case 0x35:  send_keycode(KEY_KP_SLASH,      scancode & 0x80);   break;
        case 0x38:  send_keycode(KEY_RIGHTALT,      scancode & 0x80);   break;
        case 0x47:  send_keycode(KEY_HOME,          scancode & 0x80);   break;
        case 0x48:  send_keycode(KEY_UP,            scancode & 0x80);   break;
        case 0x49:  send_keycode(KEY_PAGEUP,        scancode & 0x80);   break;
        case 0x4b:  send_keycode(KEY_LEFT,          scancode & 0x80);   break;
        case 0x4d:  send_keycode(KEY_RIGHT,         scancode & 0x80);   break;
        case 0x4f:  send_keycode(KEY_END,           scancode & 0x80);   break;
        case 0x50:  send_keycode(KEY_DOWN,          scancode & 0x80);   break;
        case 0x51:  send_keycode(KEY_PAGEDOWN,      scancode & 0x80);   break;
        case 0x52:  send_keycode(KEY_INSERT,        scancode & 0x80);   break;
        case 0x53:  send_keycode(KEY_DELETE,        scancode & 0x80);   break;
        case 0x5b:  send_keycode(KEY_GUI_LEFT,      scancode & 0x80);   break;
        case 0x5c:  send_keycode(KEY_GUI_RIGHT,     scancode & 0x80);   break;
        case 0x5d:  send_keycode(KEY_APPS,          scancode & 0x80);   break;
        case 0x5e:  send_keycode(KEY_ACPI_POWER,    scancode & 0x80);   break;
        case 0x5f:  send_keycode(KEY_ACPI_SLEEP,    scancode & 0x80);   break;
        case 0x63:  send_keycode(KEY_ACPI_WAKE,     scancode & 0x80);   break;
        case 0x65:  send_keycode(KEY_WWW_SEARCH,    scancode & 0x80);   break;
        case 0x66:  send_keycode(KEY_WWW_FAVORITES, scancode & 0x80);   break;
        case 0x67:  send_keycode(KEY_WWW_REFRESH,   scancode & 0x80);   break;
        case 0x68:  send_keycode(KEY_WWW_STOP,      scancode & 0x80);   break;
        case 0x69:  send_keycode(KEY_WWW_FORWARD,   scancode & 0x80);   break;
        case 0x6a:  send_keycode(KEY_WWW_BACK,      scancode & 0x80);   break;
        case 0x6b:  send_keycode(KEY_MM_MYCOMPUTER, scancode & 0x80);   break;
        case 0x6c:  send_keycode(KEY_MM_EMAIL,      scancode & 0x80);   break;
        case 0x6d:  send_keycode(KEY_MM_SELECT,     scancode & 0x80);   break;
        }
        state = STATE_NORMAL;
        break;
    case STATE_PRTSC_DOWN_2:
        if (0xe0 == scancode) {
            state = STATE_PRTSC_DOWN_3;
        } else {
            state = STATE_NORMAL;
        }
        break;
    case STATE_PRTSC_UP_2:
        if (0xe0 == scancode) {
            state = STATE_PRTSC_UP_3;
        } else {
            state = STATE_NORMAL;
        }
        break;
    case STATE_PRTSC_DOWN_3:
        if (0x37 == scancode) {
            send_keycode(KEY_PRTSC, NO);    // down
        }
        state = STATE_NORMAL;
        break;
    case STATE_PRTSC_UP_3:
        if (0xaa == scancode) {
            send_keycode(KEY_PRTSC, YES);   // up
        }
        state = STATE_NORMAL;
        break;
    case STATE_PAUSE_1:
        if (0x1d == scancode) {
            state = STATE_PAUSE_2;
        } else {
            state = STATE_NORMAL;
        }
        break;
    case STATE_PAUSE_2:
        if (0x45 == scancode) {
            state = STATE_PAUSE_3;
        } else {
            state = STATE_NORMAL;
        }
        break;
    case STATE_PAUSE_3:
        if (0xe1 == scancode) {
            state = STATE_PAUSE_4;
        } else {
            state = STATE_NORMAL;
        }
        break;
    case STATE_PAUSE_4:
        if (0x9d == scancode) {
            state = STATE_PAUSE_5;
        } else {
            state = STATE_NORMAL;
        }
        break;
    case STATE_PAUSE_5:
        if (0xc5 == scancode) {
            send_keycode(KEY_PAUSE, NO);
        }
        state = STATE_NORMAL;
        break;
    default:
        break;
    }
}

static void ps2kbd_int_handler(int vec __UNUSED, int_frame_t * sp __UNUSED) {
    while (in8(PS2KBD_CTRL_PORT) & 1) {
        u8 scancode = in8(PS2KBD_DATA_PORT);
        digest_scan_code(scancode);
    }
    loapic_send_eoi();
}

__INIT void ps2kbd_dev_init() {
    int gsi = ioapic_irq_to_gsi(1);
    int vec = ioapic_gsi_to_vec(gsi);
    isr_tbl[vec] = ps2kbd_int_handler;
    state = STATE_NORMAL;
    ioapic_gsi_unmask(gsi);
}
