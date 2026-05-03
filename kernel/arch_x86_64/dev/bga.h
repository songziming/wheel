#ifndef ARCH_X86_64_DEV_BGA_H
#define ARCH_X86_64_DEV_BGA_H

#include <wheel.h>

// BGA 默认 framebuffer 物理地址（QEMU/Bochs 默认值，PCI BAR0 也指向此处）
// 老版本 qemu 映射地址是 0xE0000000，还是应该读取 PCI-Bar0
#define BGA_FB_ADDR     0xfd000000
#define BGA_FB_SIZE     (16U << 20)

INIT_TEXT int bga_check();  // 探测 BGA
INIT_TEXT int bga_config(uint32_t w, uint32_t h, uint32_t bpp, uint32_t vw, uint32_t vh);
INIT_TEXT void bga_disable();

void bga_set_offset(uint16_t x, uint16_t y);

#endif // ARCH_X86_64_DEV_BGA_H
