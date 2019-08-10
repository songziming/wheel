#include <wheel.h>

// text user interface, the iodev that programs read from and write to.

// this module receives events using `event.c` from kbd and mouse
// then translate raw keycode into ascii (optional).

// this module also receives output from user programs, with support
// for escape sequences like setting caret position and text color.

//------------------------------------------------------------------------------
// keycode to ascii conversion

// keyboard lock content, scroll lock is omitted
static int kbd_capslock = 0;
static int kbd_numlock  = 1;

// we have to track the state of modifier keys
static int kbd_l_shift   = 0;
static int kbd_r_shift   = 0;
static int kbd_l_control = 0;
static int kbd_r_control = 0;
static int kbd_l_alt     = 0;
static int kbd_r_alt     = 0;

// notice that 0 appears first, different from keyboard layout
static const char syms[] = ")!@#$%^&*(";

// convert keycode to ascii, return -1 if no ascii
static char keycode_to_ascii(keycode_t code, int release) {
    if (release) {
        switch (code) {
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

    switch (code) {
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
            return 'A' + (code - KEY_A);
        } else {
            return 'a' + (code - KEY_A);
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
            return syms[code - KEY_0];
        } else {
            return '0' + (code - KEY_0);
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
// read write function

static int       tty_xlate = YES;   // converts keycode to ascii
static int       tty_echo  = YES;   // show input on screen
static fdesc_t * stdin_r   = NULL;  // actual pipe that holds input
static fdesc_t * stdin_w   = NULL;  // write end

// blocking read
static usize tty_read(iodev_t * tty __UNUSED, u8 * buf, usize len) {
    return ios_read(stdin_r, buf, len);
}

// non blocking write
static usize tty_write(iodev_t * tty __UNUSED, const u8 * buf, usize len) {
    // TODO: parse buf, detect and handle escape sequences
    // TODO: don't use debug function, call console driver directly
    dbg_print("%*s", len, buf);
    return len;
}

// listen from event and pipe data to stdin
// bottom half of keyboard ISR
static void listener_proc() {
    while (1) {
        keycode_t code = kbd_recv();
        if (YES == tty_xlate) {
            char ch = keycode_to_ascii(code & 0x7fffffff, code & 0x80000000);
            if ((char) -1 == ch) {
                continue;
            }
            ios_write(stdin_w, &ch, sizeof(char));
            if (YES == tty_echo) {
                dbg_print("%c", ch);
            }
        } else {
            ios_write(stdin_w, &code, sizeof(keycode_t));
        }
    }
}

//------------------------------------------------------------------------------
// tty device driver

static const iodrv_t tty_drv = {
    .read  = (ios_read_t)  tty_read,
    .write = (ios_write_t) tty_write,
    .lseek = (ios_lseek_t) NULL,
};

// singleton object
static iodev_t tty_dev = {
    .ref  = 1,
    .free = (ios_free_t) NULL,
    .drv  = (iodrv_t *) &tty_drv,
};

iodev_t * tty_get_instance() {
    return &tty_dev;
}

__INIT void tty_dev_init() {
    tty_dev.ref  = 1;
    tty_dev.free = (ios_free_t) NULL;
    tty_dev.drv  = (iodrv_t *) &tty_drv;

    // TODO: regist tty_dev into vfs as `/dev/tty`, so that any
    //       user program could access it.

    stdin_r = ios_open("/dev/kbd", IOS_READ);
    stdin_w = ios_open("/dev/kbd", IOS_WRITE);
    task_resume(task_create(0, listener_proc, 0,0,0,0));
}
