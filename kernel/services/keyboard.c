#include "keyboard.h"
#include <wheel.h>
#include <msgq.h>

static msgq_t keys;

INIT_TEXT void keyboard_init() {
    msgq_init(&keys);
}

// called in ISR
void send_keycode(keycode_t kc) {
    msgq_send_force(&keys, &kc, sizeof(keycode_t));
}

keycode_t get_keycode() {
    keycode_t code;
    size_t len = msgq_recv(&keys, &code, sizeof(code), FOREVER);
    if (sizeof(code) == len) {
        return code;
    }
    return KEY_RESERVED;
}
