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

#if (CFG_TTY_BUFF_SIZE & (CFG_TTY_BUFF_SIZE - 1)) != 0
    #error "CFG_TTY_BUFF_SIZE must be power of 2"
#endif

typedef struct pender {
    dlnode_t dl;
    task_t * tid;
} pender_t;

static int      tty_xlate = YES;    // converts keycode to ascii
static int      tty_echo  = YES;    // show input on screen

// tty input buffer, fixed size
static char     tty_buff[CFG_TTY_BUFF_SIZE];
static spin_t   tty_spin    = SPIN_INIT;
static dllist_t tty_penders = DLLIST_INIT;
static fifo_t   tty_fifo    = FIFO_INIT(tty_buff, CFG_TTY_BUFF_SIZE * sizeof(char));

// wakeup pended readers
static void ready_read() {
    while (1) {
        dlnode_t * head = dl_pop_head(&tty_penders);
        if (NULL == head) {
            return;
        }

        pender_t * pender = PARENT(head, pender_t, dl);
        task_t   * tid    = pender->tid;

        raw_spin_take(&tid->spin);
        sched_cont(tid, TS_PEND);
        int cpu = tid->last_cpu;
        raw_spin_give(&tid->spin);

        if (cpu_index() != cpu) {
            smp_resched(cpu);
        }
    }
}

// blocking read
static usize tty_read(iodev_t * tty __UNUSED, u8 * buf, usize len, usize * pos __UNUSED) {
    // return ios_read(stdin_r, buf, len);
    while (1) {
        u32   key = irq_spin_take(&tty_spin);
        usize got = fifo_read(&tty_fifo, buf, len);
        if (0 != got) {
            irq_spin_give(&tty_spin, key);
            return got;
        }

        // pend current task
        task_t * tid    = thiscpu_var(tid_prev);
        pender_t pender = {
            .dl  = DLNODE_INIT,
            .tid = tid,
        };
        dl_push_tail(&tty_penders, &pender.dl);

        // pend here and try again
        raw_spin_take(&tid->spin);
        sched_stop(tid, TS_PEND);
        raw_spin_give(&tid->spin);
        irq_spin_give(&tty_spin, key);
        task_switch();
    }
}

// non blocking write
static usize tty_write(iodev_t * tty __UNUSED, const u8 * buf, usize len, usize * pos __UNUSED) {
    // TODO: parse buf, detect and handle escape sequences
    // TODO: don't use debug function, call console driver directly
    dbg_print("%*s", len, buf);
    return len;
}

// listen from event and pipe data to stdin
// bottom half of keyboard ISR
// if tty input buffer is full, then new data is discarded
static void listener_proc() {
    while (1) {
        keycode_t code = kbd_recv();
        if (YES == tty_xlate) {
            char ch = keycode_to_ascii(code & 0x7fffffff, code & 0x80000000);
            if ((char) -1 == ch) {
                continue;
            }
            if (YES == tty_echo) {
                dbg_print("%c", ch);
            }

            u32 key = irq_spin_take(&tty_spin);
            fifo_write(&tty_fifo, (u8 *) &ch, sizeof(char), NO);

            // notify readers if tty buffer full or hit enter
            if (fifo_is_full(&tty_fifo) || ('\n' == ch)) {
                ready_read();
                irq_spin_give(&tty_spin, key);
                task_switch();
            } else {
                irq_spin_give(&tty_spin, key);
            }
        } else {
            u32   key = irq_spin_take(&tty_spin);
            usize len = fifo_write(&tty_fifo, (u8 *) &code, sizeof(keycode_t), NO);
            if (0 != len) {
                ready_read();
                irq_spin_give(&tty_spin, key);
                task_switch();
            } else {
                irq_spin_give(&tty_spin, key);
            }
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

// singleton object, no custom fields
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

    task_resume(task_create(0, listener_proc, 0,0,0,0));
}
