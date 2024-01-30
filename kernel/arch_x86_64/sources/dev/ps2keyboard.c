// 将 scancode 转换成 keycode，并发送给 tty 设备文件

#include <wheel.h>
#include <cpu/rw.h>
#include <cpu/io_apic.h>
#include <cpu/local_apic.h>
#include <arch_smp.h>
#include <arch_config.h>
#include <arch_int.h>



// PS/2 键盘通过串口和计算机通信
// 在早期 IBM PC 上，负责与 PS/2 键盘通信的是 Intel 8042 芯片
// 后来被集成到 Advanced Integrated Peripheral 芯片中（如 82091AA）
// 中断发送给 IRQ 1


#define PS2KBD_CTRL_PORT 0x64
#define PS2KBD_DATA_PORT 0x60



static void handle_keyboard(int vec, arch_regs_t *f) {
    (void)vec;
    (void)f;

    klog("keyboard interrupt from cpu %d, vec %d\n", cpu_index(), vec);
    // klog("local apic tmr is %d\n", local_apic_get_tmr(vec));

    while (in8(PS2KBD_CTRL_PORT) & 1) {
        uint8_t scancode = in8(PS2KBD_DATA_PORT);
        klog("scan code %x\n", scancode);
    }

    local_apic_send_eoi();
}


INIT_TEXT void keyboard_init() {
    int gsi = get_gsi_for_irq(1);

#if 0
    klog("GSI for keyboard is %d\n", gsi);
    const char *trigger = get_gsi_trigmode(gsi) ? "edge" : "level";
    const char *active = get_gsi_polarity(gsi) ? "high" : "low";
    klog("config is %s-trigger, active-%s\n", trigger, active);
#endif

    set_int_handler(gsi + VEC_GSI_BASE, handle_keyboard);
    io_apic_unmask_gsi(gsi);
}