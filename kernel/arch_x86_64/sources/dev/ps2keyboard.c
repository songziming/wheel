// 将 scancode 转换成 keycode，并发送给 tty 设备文件

#include <wheel.h>
#include <cpu/rw.h>
#include <arch_smp.h>
#include <arch_config.h>
#include <arch_int.h>



#define PS2KBD_CTRL_PORT 0x64
#define PS2KBD_DATA_PORT 0x60

static void handle_keyboard(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;

    klog("keyboard interrupt from cpu %d\n", cpu_index());

    while (in8(PS2KBD_CTRL_PORT) & 1) {
        uint8_t scancode = in8(PS2KBD_DATA_PORT);
        klog("scan code %x\n", scancode);
    }
}


INIT_TEXT void keyboard_init() {
    int gsi = get_gsi_for_irq(1);
    int vec = VEC_GSI_BASE + gsi;

    set_int_handler(vec, handle_keyboard);
}
